#pragma once

#include <spectra/audio/buffer.hpp>
#include <spectra/core/result.hpp>
#include <spectra/core/types.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>

namespace spectra::audio {

/// The two corrections between "what the speakers are playing" and "what the
/// screen should be showing".
struct ClockOffsets {
    /// What the output device reports. Buffered frames have been handed over
    /// but not yet heard.
    Seconds deviceLatency{0.0};

    /// The manual nudge.
    ///
    /// It exists because Bluetooth headphones add 150–300 ms that the device
    /// under-reports, and without a way to correct for it the preview looks
    /// wrong to the user through no fault of ours.
    Seconds userOffset{0.0};
};

[[nodiscard]] inline Seconds visualTime(Seconds audioClock, const ClockOffsets& offsets) {
    return audioClock - offsets.deviceLatency + offsets.userOffset;
}

/// Where the playhead is, and what a read does to it.
///
/// Split out from `Player` because this is the part with the interesting
/// behaviour — wrapping, clamping, running off the end — and `Player` needs
/// a sound card to exist at all. This needs nothing, so it is tested
/// everywhere rather than only on a developer's desk.
///
/// The position is atomic because the audio thread advances it and the UI
/// thread reads it. Relaxed ordering is enough: nothing else is published
/// alongside it, and a reader a few frames behind is invisible.
class PlaybackCursor {
public:
    explicit PlaybackCursor(std::size_t frameCount = 0) : frameCount_(frameCount) {}

    void setFrameCount(std::size_t frameCount) { frameCount_ = frameCount; }

    [[nodiscard]] std::size_t frameCount() const { return frameCount_; }

    [[nodiscard]] std::size_t position() const { return position_.load(std::memory_order_relaxed); }

    void seek(std::size_t frame) {
        position_.store(std::min(frame, frameCount_), std::memory_order_relaxed);
    }

    [[nodiscard]] bool finished() const { return position() >= frameCount_; }

    void setLooping(bool looping) { looping_.store(looping, std::memory_order_relaxed); }

    [[nodiscard]] bool looping() const { return looping_.load(std::memory_order_relaxed); }

    /// Copies `frames` frames into `out` and advances. Anything past the end
    /// of the track is written as silence rather than left alone, because the
    /// device's buffer is not guaranteed to arrive zeroed and whatever is in
    /// it will be audible.
    ///
    /// Returns the number of real frames written, which is fewer than asked
    /// for only at the end of a track that is not looping.
    std::size_t
    read(const AudioBuffer& audio, std::span<float> out, std::size_t frames, float volume = 1.0f);

private:
    std::size_t frameCount_{0};
    std::atomic<std::size_t> position_{0};
    std::atomic<bool> looping_{false};
};

struct PlayerOptions {
    float volume{1.0f};
    bool loop{false};
};

/// Plays a decoded track and reports where it has got to.
///
/// There is no ring buffer and no decoder thread, which is a deliberate
/// departure from the sketch in docs/04-AUDIO-ENGINE.md. That design assumes
/// streaming, and we decode whole tracks up front for the analysis anyway —
/// so the buffer is already there, immutable, and the audio callback can read
/// straight out of it behind one atomic cursor. A ring buffer would add a
/// producer thread, a fill policy and an underrun case to solve a problem we
/// do not have.
///
/// The audio thread does nothing but copy and multiply: no allocation, no
/// locks, no logging, no analysis.
class Player {
public:
    ~Player();

    Player(Player&&) noexcept;
    Player& operator=(Player&&) noexcept;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    /// The buffer is shared rather than copied or referenced: the audio
    /// thread reads it for as long as the device is open, and an owner who
    /// drops it early would be a use-after-free in a realtime callback.
    [[nodiscard]] static Result<Player> create(std::shared_ptr<const AudioBuffer> audio,
                                               const PlayerOptions& options = {});

    void play();
    void pause();

    [[nodiscard]] bool playing() const;

    void seek(Seconds t);

    /// Where the audio output has actually got to.
    [[nodiscard]] Seconds position() const;

    [[nodiscard]] Seconds duration() const;

    /// `position()` corrected for latency: what the visuals should show.
    [[nodiscard]] Seconds visualPosition() const;

    [[nodiscard]] const ClockOffsets& offsets() const;

    void setUserOffset(Seconds offset);

    void setVolume(float volume);

    [[nodiscard]] float volume() const;

    void setLooping(bool looping);

    /// The device's own sample rate, which need not be the track's — the
    /// backend resamples.
    [[nodiscard]] int deviceSampleRate() const;

private:
    struct Impl;

    explicit Player(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace spectra::audio
