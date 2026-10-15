#pragma once

#include <spectra/core/types.hpp>

#include <cstdint>
#include <span>
#include <vector>

/// Signal-processing primitives: windows, band layouts, smoothing, envelopes,
/// spectral tilt.
///
/// Everything here is a pure function of its arguments, which is deliberate.
/// The analysis pass is the part that has to be deterministic
/// (docs/04-AUDIO-ENGINE.md), and the cheapest way to get there is for the
/// pieces it is built from to hold no state at all.
namespace spectra::audio {

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

enum class WindowShape : std::uint8_t {
    /// The default. Good frequency resolution, mild leakage, no scalloping
    /// surprises — and what Avee uses, so an imported scene reads similarly.
    Hann,
    /// Slightly lower first sidelobe than Hann, slightly worse far rejection.
    /// Used for the across-band smoothing kernel rather than for analysis.
    Hamming,
    /// Very low sidelobes at the cost of a wide main lobe. Worth having for
    /// anyone chasing a clean spectrum rather than a responsive one.
    BlackmanHarris,
};

/// Fills `window` with the shape, sampled over its full length.
void fillWindow(std::span<float> window, WindowShape shape);

/// Mean of the window. Dividing a windowed FFT magnitude by this restores the
/// amplitude a sine would have had unwindowed, which is what makes a band
/// value comparable across window shapes.
[[nodiscard]] float coherentGain(std::span<const float> window);

// ---------------------------------------------------------------------------
// Band layout
// ---------------------------------------------------------------------------

enum class BandScale : std::uint8_t {
    /// Equal width in Hz. Matches the FFT's own bins and looks, to a listener,
    /// like everything is crammed into the left tenth of the display.
    Linear,
    /// Equal ratio per band. What Avee offers and what most visualizers use.
    Logarithmic,
    /// Equal spacing on the mel scale: close to logarithmic above 1 kHz and
    /// closer to linear below it, which is nearer to how hearing actually
    /// works. Our default.
    Mel,
};

/// Band edges in Hz. `edges` always holds `count() + 1` entries, so band `i`
/// spans `[edges[i], edges[i + 1])`.
struct BandLayout {
    std::vector<float> edges;

    [[nodiscard]] int count() const { return static_cast<int>(edges.size()) - 1; }

    /// Geometric centre, which is the perceptual middle of a band on any of
    /// the scales we offer — the arithmetic mean of 20 and 40 Hz is 30, but
    /// the note halfway between them is 28.
    [[nodiscard]] float centerHz(int band) const;

    [[nodiscard]] float widthHz(int band) const;
};

[[nodiscard]] BandLayout makeBandLayout(int bandCount, float minHz, float maxHz, BandScale scale);

[[nodiscard]] float hzToMel(float hz);
[[nodiscard]] float melToHz(float mel);

/// Collapses FFT magnitudes onto bands.
///
/// Bands wider than a bin average the bins they cover. Bands narrower than a
/// bin — which is every low band on a log or mel layout at any reasonable
/// window size — interpolate between the two nearest bins instead of reading
/// zero. Without that, the bottom of a 128-band display is a comb of empty
/// slots, which is a bug people reliably mistake for "no bass".
///
/// `out` is written with `layout.count()` values.
void mapBands(std::span<const float> magnitudes,
              double binWidthHz,
              const BandLayout& layout,
              std::span<float> out);

// ---------------------------------------------------------------------------
// Smoothing across bands
// ---------------------------------------------------------------------------

/// A normalized Hamming kernel of `2 * radius + 1` taps, summing to 1.
///
/// Avee builds the same thing in SegmentAudioSpectrumData, weighting
/// neighbouring bins by a Hamming window; it is what stops adjacent bars
/// looking uncorrelated and jittery. Normalizing is our addition — Avee
/// divides by the tap count rather than the weight sum, which quietly
/// attenuates the spectrum as the kernel widens.
[[nodiscard]] std::vector<float> hammingKernel(int radius);

/// Convolves `in` with `kernel`, clamping at the edges. `out` may not alias
/// `in`.
///
/// Clamping rather than wrapping: the bottom band's neighbour is not the top
/// band, and treating the spectrum as circular smears sub-bass into treble.
void smoothAcrossBands(std::span<const float> in,
                       std::span<const float> kernel,
                       std::span<float> out);

// ---------------------------------------------------------------------------
// Smoothing across time
// ---------------------------------------------------------------------------

/// One-pole coefficients for an asymmetric envelope follower.
///
/// Expressed in seconds and converted against the analysis rate, rather than
/// being per-frame constants. Avee's `smoothFactor`/`smoothFactor2` are
/// per-rendered-frame, so their meaning changes with the frame rate; ours
/// cannot, because the analysis rate is fixed.
struct EnvelopeCoefficients {
    float attack{1.0f};
    float decay{1.0f};
};

/// `attackTime` and `decayTime` are the time to cover 1 - 1/e of the distance
/// to the target. Zero means instantaneous.
[[nodiscard]] EnvelopeCoefficients
envelopeCoefficients(Seconds attackTime, Seconds decayTime, double rateHz);

/// One step of the follower: fast up, slow down.
[[nodiscard]] float
followEnvelope(float current, float target, const EnvelopeCoefficients& coefficients);

// ---------------------------------------------------------------------------
// Spectral tilt
// ---------------------------------------------------------------------------

/// Compensation for the fact that music is not flat.
///
/// Musical audio is roughly pink — power proportional to 1/f — so raw FFT
/// magnitude always shows bass dominating, regardless of the mix. This is the
/// main cause of the "Avee bass bug" described in docs/04-AUDIO-ENGINE.md, and
/// it is a display problem rather than an analysis error.
enum class SpectralTilt : std::uint8_t {
    /// No compensation. Avee's look, and what its users expect.
    Flat,
    /// +3 dB per octave, so pink noise reads as a flat line.
    Pink,
    /// A-weighting, which also accounts for the ear being less sensitive to
    /// bass at ordinary listening levels.
    Perceptual,
};

/// Linear amplitude gain at `hz`, normalized to 1.0 at 1 kHz.
[[nodiscard]] float tiltGain(SpectralTilt tilt, float hz);

/// Per-band gains for a layout, evaluated at each band's centre.
[[nodiscard]] std::vector<float> tiltGains(SpectralTilt tilt, const BandLayout& layout);

}  // namespace spectra::audio
