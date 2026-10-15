#include <spectra/audio/buffer.hpp>

namespace spectra::audio {

float AudioBuffer::at(std::size_t frame, int channel) const {
    if (channel < 0 || channel >= channels) {
        return 0.0f;
    }
    const std::size_t index =
        frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
    return index < samples.size() ? samples[index] : 0.0f;
}

std::vector<float> downmixToMono(const AudioBuffer& buffer) {
    const std::size_t frames = buffer.frameCount();
    std::vector<float> mono(frames, 0.0f);
    if (buffer.channels <= 0) {
        return mono;
    }
    if (buffer.channels == 1) {
        mono.assign(buffer.samples.begin(),
                    buffer.samples.begin() + static_cast<std::ptrdiff_t>(frames));
        return mono;
    }

    const auto channels = static_cast<std::size_t>(buffer.channels);
    const float scale = 1.0f / static_cast<float>(buffer.channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float sum = 0.0f;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            sum += buffer.samples[frame * channels + channel];
        }
        mono[frame] = sum * scale;
    }
    return mono;
}

}  // namespace spectra::audio
