#pragma once

#include <spectra/core/types.hpp>

#include <cstddef>
#include <vector>

namespace spectra::audio {

/// Decoded PCM: interleaved 32-bit float, nominally in [-1, 1].
///
/// Interleaved rather than planar because that is what both miniaudio and our
/// own downmix want, and the one consumer that prefers planar — the FFT — only
/// ever looks at a mono downmix anyway.
///
/// Not clamped. Sources mastered into clipping, and lossy codecs decoding
/// slightly past full scale, both legitimately exceed 1.0, and throwing that
/// away here would hide it from the loudness statistics that want to see it.
struct AudioBuffer {
    std::vector<float> samples;
    int sampleRate{0};
    int channels{0};

    [[nodiscard]] std::size_t frameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }

    [[nodiscard]] bool empty() const { return samples.empty(); }

    [[nodiscard]] Seconds duration() const {
        return sampleRate > 0 ? static_cast<Seconds>(frameCount()) / sampleRate : 0.0;
    }

    /// Out-of-range reads give silence rather than undefined behaviour, so
    /// that windowed analysis can run off the end of the track without every
    /// caller writing the same bounds check.
    [[nodiscard]] float at(std::size_t frame, int channel) const;
};

/// Channel average, which is the right downmix for analysis.
///
/// Summing instead would make a stereo track read 6 dB louder than the same
/// material in mono, and the band energies feed a normalizer that should not
/// care how many channels the file happens to have.
///
/// Out-of-phase stereo cancels under averaging. That is a real artefact and it
/// is also what the listener hears on a mono system, so it is left alone.
[[nodiscard]] std::vector<float> downmixToMono(const AudioBuffer& buffer);

}  // namespace spectra::audio
