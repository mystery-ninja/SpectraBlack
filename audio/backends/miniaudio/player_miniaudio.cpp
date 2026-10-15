// The only translation unit in the project that includes miniaudio. The
// implementation itself is compiled separately as C, so that a 90,000-line
// third-party header is not subjected to our warning levels.

#include <spectra/audio/playback.hpp>

#include <algorithm>
#include <string>

#include <miniaudio.h>

namespace spectra::audio {

struct Player::Impl {
    std::shared_ptr<const AudioBuffer> audio;
    PlaybackCursor cursor;
    ClockOffsets offsets;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> playing{false};
    ma_device device{};
    bool deviceOpen{false};
    int deviceSampleRate{0};

    ~Impl() {
        if (deviceOpen) {
            ma_device_uninit(&device);
        }
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    /// The realtime callback. A member rather than a free function only
    /// because `Impl` is private to `Player`.
    static void
    onFrames(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount) {
        auto* impl = static_cast<Impl*>(device->pUserData);
        const auto channels = static_cast<std::size_t>(impl->audio->channels);
        auto out = std::span<float>(static_cast<float*>(output),
                                    static_cast<std::size_t>(frameCount) * channels);

        if (!impl->playing.load(std::memory_order_relaxed)) {
            std::fill(out.begin(), out.end(), 0.0f);
            return;
        }

        const std::size_t written = impl->cursor.read(
            *impl->audio, out, frameCount, impl->volume.load(std::memory_order_relaxed));

        // Running off the end stops playback, rather than leaving a stopped
        // playhead claiming it is still playing.
        if (written < frameCount && !impl->cursor.looping()) {
            impl->playing.store(false, std::memory_order_relaxed);
        }
    }
};

namespace {

/// What the device has accepted but not yet played.
///
/// miniaudio exposes the internal period size and count, which is the honest
/// figure: anything already handed to the driver is heard later than it was
/// written, and the visuals have to be delayed to match.
Seconds latencyOf(const ma_device& device) {
    const ma_uint32 rate = device.playback.internalSampleRate;
    if (rate == 0) {
        return 0.0;
    }
    const double frames = static_cast<double>(device.playback.internalPeriodSizeInFrames) *
                          device.playback.internalPeriods;
    return frames / rate;
}

}  // namespace

Player::Player(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Player::~Player() = default;

Player::Player(Player&&) noexcept = default;

Player& Player::operator=(Player&&) noexcept = default;

Result<Player> Player::create(std::shared_ptr<const AudioBuffer> audio,
                              const PlayerOptions& options) {
    if (!audio) {
        return Result<Player>::error("no audio to play");
    }
    if (audio->channels <= 0 || audio->sampleRate <= 0) {
        return Result<Player>::error("the audio buffer has no channel count or no sample rate");
    }

    auto impl = std::make_unique<Impl>();
    impl->audio = std::move(audio);
    impl->cursor.setFrameCount(impl->audio->frameCount());
    impl->cursor.setLooping(options.loop);
    impl->volume.store(options.volume);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(impl->audio->channels);
    config.sampleRate = static_cast<ma_uint32>(impl->audio->sampleRate);
    config.dataCallback = &Impl::onFrames;
    config.pUserData = impl.get();

    if (const ma_result status = ma_device_init(nullptr, &config, &impl->device);
        status != MA_SUCCESS) {
        return Result<Player>::error(std::string("no audio output device: ") +
                                     ma_result_description(status));
    }
    impl->deviceOpen = true;
    impl->deviceSampleRate = static_cast<int>(impl->device.sampleRate);
    impl->offsets.deviceLatency = latencyOf(impl->device);

    // Started immediately and left running, with the callback writing silence
    // while paused. Starting and stopping the device on every transport press
    // costs tens of milliseconds and, on some backends, a click.
    if (const ma_result status = ma_device_start(&impl->device); status != MA_SUCCESS) {
        return Result<Player>::error(std::string("cannot start the audio device: ") +
                                     ma_result_description(status));
    }

    return Result<Player>::ok(Player(std::move(impl)));
}

void Player::play() {
    if (impl_->cursor.finished() && !impl_->cursor.looping()) {
        impl_->cursor.seek(0);
    }
    impl_->playing.store(true, std::memory_order_relaxed);
}

void Player::pause() {
    impl_->playing.store(false, std::memory_order_relaxed);
}

bool Player::playing() const {
    return impl_->playing.load(std::memory_order_relaxed);
}

void Player::seek(Seconds t) {
    const double frame = std::max(t, 0.0) * impl_->audio->sampleRate;
    impl_->cursor.seek(static_cast<std::size_t>(frame));
}

Seconds Player::position() const {
    return static_cast<Seconds>(impl_->cursor.position()) / impl_->audio->sampleRate;
}

Seconds Player::duration() const {
    return impl_->audio->duration();
}

Seconds Player::visualPosition() const {
    return visualTime(position(), impl_->offsets);
}

const ClockOffsets& Player::offsets() const {
    return impl_->offsets;
}

void Player::setUserOffset(Seconds offset) {
    impl_->offsets.userOffset = offset;
}

void Player::setVolume(float volume) {
    impl_->volume.store(volume, std::memory_order_relaxed);
}

float Player::volume() const {
    return impl_->volume.load(std::memory_order_relaxed);
}

void Player::setLooping(bool looping) {
    impl_->cursor.setLooping(looping);
}

int Player::deviceSampleRate() const {
    return impl_->deviceSampleRate;
}

}  // namespace spectra::audio
