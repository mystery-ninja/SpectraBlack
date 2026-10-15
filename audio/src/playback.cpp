#include <spectra/audio/playback.hpp>

#include <algorithm>

namespace spectra::audio {

std::size_t PlaybackCursor::read(const AudioBuffer& audio,
                                 std::span<float> out,
                                 std::size_t frames,
                                 float volume) {
    const auto channels = static_cast<std::size_t>(std::max(audio.channels, 0));
    const std::size_t wanted = std::min(frames, channels > 0 ? out.size() / channels : 0);
    if (wanted == 0) {
        std::fill(out.begin(), out.end(), 0.0f);
        return 0;
    }

    std::size_t written = 0;
    std::size_t position = this->position();
    const bool loop = looping();

    while (written < wanted) {
        if (position >= frameCount_) {
            if (!loop || frameCount_ == 0) {
                break;
            }
            position = 0;
        }

        const std::size_t run = std::min(wanted - written, frameCount_ - position);
        const auto source =
            audio.samples.begin() + static_cast<std::ptrdiff_t>(position * channels);
        const auto destination = out.begin() + static_cast<std::ptrdiff_t>(written * channels);
        std::transform(source,
                       source + static_cast<std::ptrdiff_t>(run * channels),
                       destination,
                       [volume](float sample) { return sample * volume; });

        position += run;
        written += run;
    }

    // Past the end. The device's buffer is not guaranteed to arrive zeroed,
    // and whatever is left in it is audible.
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(written * channels), out.end(), 0.0f);

    position_.store(position, std::memory_order_relaxed);
    return written;
}

}  // namespace spectra::audio
