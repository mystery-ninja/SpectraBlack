#pragma once

#include <spectra/core/result.hpp>

#include <memory>
#include <span>

namespace spectra::audio {

/// Real-input forward FFT, producing magnitudes.
///
/// Wraps PFFFT. The header does not say so, and the wrapper exists mostly for
/// that reason: PFFFT wants SIMD-aligned buffers, its own scratch space, and a
/// packing where the Nyquist bin rides in the imaginary part of bin zero. None
/// of that should be visible to the analysis code.
///
/// Forward only. Nothing in this project needs to go back to the time domain —
/// we measure spectra, we do not resynthesize.
///
/// One instance is not thread-safe (it owns scratch buffers), but instances are
/// independent, which is what the chunked parallel analysis in
/// docs/04-AUDIO-ENGINE.md needs: one per worker.
class RealFft {
public:
    ~RealFft();

    RealFft(RealFft&&) noexcept;
    RealFft& operator=(RealFft&&) noexcept;
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;

    /// PFFFT accepts N = 2^a * 3^b * 5^c with a >= 5, so 2048 is fine and
    /// 2000 is not. Checked rather than asserted, because window size is
    /// reachable from a project file.
    [[nodiscard]] static bool isSupportedSize(int size);

    [[nodiscard]] static Result<RealFft> create(int size);

    [[nodiscard]] int size() const { return size_; }

    /// Number of magnitudes `transform` writes: DC through Nyquist inclusive.
    [[nodiscard]] int binCount() const { return size_ / 2 + 1; }

    /// Width of one bin in Hz, for a given sample rate.
    [[nodiscard]] double binWidth(double sampleRate) const { return sampleRate / size_; }

    /// `input` must hold exactly `size()` samples and `magnitudes` exactly
    /// `binCount()`. Magnitudes are unnormalized: a full-scale sine in the
    /// centre of a bin reads `size()/2 * amplitude` before windowing.
    void transform(std::span<const float> input, std::span<float> magnitudes) const;

private:
    struct Impl;

    RealFft(std::unique_ptr<Impl> impl, int size);

    std::unique_ptr<Impl> impl_;
    int size_{0};
};

}  // namespace spectra::audio
