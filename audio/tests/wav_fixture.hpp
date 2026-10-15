#pragma once

// Test fixtures are generated, not committed. Audio files are binary, they do
// not diff, and git keeps every version of them forever
// (docs/GIT-CHEATSHEET.md). A sine wave written by eight lines of header
// construction is also more useful than a recording, because we know exactly
// what should come back out.

#include <spectra/audio/buffer.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

namespace spectra::audio::test {

/// A directory under the system temp dir that removes itself.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("spectra-audio-" + std::to_string(now) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] std::filesystem::path file(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

enum class WavSampleType { Int16, Float32 };

namespace detail {

inline void putU32(std::vector<char>& out, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.push_back(static_cast<char>((value >> (8 * byte)) & 0xFFu));
    }
}

inline void putU16(std::vector<char>& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFFu));
    out.push_back(static_cast<char>((value >> 8) & 0xFFu));
}

inline void putTag(std::vector<char>& out, const char* tag) {
    for (int index = 0; index < 4; ++index) {
        out.push_back(tag[index]);
    }
}

}  // namespace detail

/// Writes a canonical 44-byte-header WAV. `Float32` round-trips bit-exactly,
/// which is what lets a decode test assert equality rather than nearness.
inline void
writeWav(const std::filesystem::path& path, const AudioBuffer& buffer, WavSampleType type) {
    const std::uint16_t bitsPerSample = type == WavSampleType::Float32 ? 32 : 16;
    const std::uint16_t formatTag = type == WavSampleType::Float32 ? 3 : 1;  // IEEE float, or PCM
    const auto channels = static_cast<std::uint16_t>(buffer.channels);
    const auto rate = static_cast<std::uint32_t>(buffer.sampleRate);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bitsPerSample / 8);
    const auto dataBytes = static_cast<std::uint32_t>(buffer.samples.size() * (bitsPerSample / 8));

    std::vector<char> bytes;
    bytes.reserve(44 + dataBytes);

    detail::putTag(bytes, "RIFF");
    detail::putU32(bytes, 36 + dataBytes);
    detail::putTag(bytes, "WAVE");
    detail::putTag(bytes, "fmt ");
    detail::putU32(bytes, 16);
    detail::putU16(bytes, formatTag);
    detail::putU16(bytes, channels);
    detail::putU32(bytes, rate);
    detail::putU32(bytes, rate * blockAlign);
    detail::putU16(bytes, blockAlign);
    detail::putU16(bytes, bitsPerSample);
    detail::putTag(bytes, "data");
    detail::putU32(bytes, dataBytes);

    for (const float sample : buffer.samples) {
        if (type == WavSampleType::Float32) {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(sample));
            std::memcpy(&bits, &sample, sizeof(bits));
            detail::putU32(bytes, bits);
        } else {
            const float clamped = sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
            const auto quantized = static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
            detail::putU16(bytes, static_cast<std::uint16_t>(quantized));
        }
    }

    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/// A sine at `frequency` Hz, identical in every channel.
inline AudioBuffer
makeSine(double frequency, double seconds, int sampleRate, int channels, float amplitude = 0.5f) {
    AudioBuffer buffer{
        .samples = {},
        .sampleRate = sampleRate,
        .channels = channels,
    };
    const auto frames = static_cast<std::size_t>(seconds * sampleRate);
    buffer.samples.resize(frames * static_cast<std::size_t>(channels));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double phase =
            2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
        const auto value = static_cast<float>(std::sin(phase)) * amplitude;
        for (int channel = 0; channel < channels; ++channel) {
            buffer.samples[frame * static_cast<std::size_t>(channels) +
                           static_cast<std::size_t>(channel)] = value;
        }
    }
    return buffer;
}

}  // namespace spectra::audio::test
