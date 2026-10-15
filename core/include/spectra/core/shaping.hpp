#pragma once

#include <spectra/core/signal.hpp>
#include <spectra/core/types.hpp>

#include <cstdint>

namespace spectra {

/// Rescale an input range onto an output range, optionally flipping it.
/// Output is not clamped — a driver that overshoots is often the point.
struct Remap {
    float inMin{0.0f};
    float inMax{1.0f};
    float outMin{0.0f};
    float outMax{1.0f};
    bool invert{false};

    [[nodiscard]] float apply(float v) const;
};

enum class CurveKind : std::uint8_t {
    Linear,
    Exponential,  ///< `pow(v, exponent)`. The difference between mushy and punchy.
    Smoothstep,
};

struct Curve {
    CurveKind kind{CurveKind::Linear};
    float exponent{2.0f};  ///< Only read when `kind == Exponential`.

    [[nodiscard]] float apply(float v) const;
};

/// Per-driver envelope on top of whatever smoothing the analysis already did.
///
/// Both values are time constants in seconds. Zero means instantaneous.
struct Envelope {
    float attack{0.0f};  ///< Time constant while the signal is rising.
    float decay{0.0f};   ///< Time constant while it is falling.

    [[nodiscard]] bool active() const { return attack > 0.0f || decay > 0.0f; }
};

/// A fresh random value per beat, rather than per frame.
///
/// Seeded from (`seed`, beat index) so that scrubbing backwards reproduces the
/// same values. A running RNG would make the result depend on how many frames
/// had been drawn, which makes export disagree with preview. This generalizes
/// Avee's `BeatRandomShake`.
struct RandomStage {
    bool enabled{false};
    std::uint32_t seed{0};
    float amount{1.0f};  ///< 0 is a no-op, 1 fully scales by the random value.

    [[nodiscard]] float apply(float v, std::int64_t beat) const;
};

/// The ordered path from a raw signal to a driver's contribution:
///
///     signal → remap → curve → attack/decay → delay → random → out
class ShapingChain {
public:
    Remap remap;
    Curve curve;
    Envelope envelope;
    double delaySeconds{0.0};
    RandomStage random;

    /// Sample `ref` through the full chain at `t`.
    ///
    /// Takes the provider rather than an already-sampled value because the
    /// envelope stage needs the signal's recent history, not just its value
    /// now. See the note on determinism in the implementation.
    [[nodiscard]] float apply(const SignalProvider& signals, const SignalRef& ref, Seconds t) const;

    /// Everything up to but not including the envelope. Exposed for tests.
    [[nodiscard]] float applyStatic(float raw) const { return curve.apply(remap.apply(raw)); }
};

/// Rate at which the envelope stage walks the signal's history. Matches the
/// analysis hop in docs/04-AUDIO-ENGINE.md so that, once Stage 3 lands, the
/// walk lands on real analysis frames instead of interpolating between them.
inline constexpr double kShapingHopSeconds = 1.0 / 200.0;

/// How many time constants of history the envelope walks before trusting its
/// output. After 5τ the error from the unknown starting value is under 1%.
inline constexpr double kEnvelopeWarmupTimeConstants = 5.0;

/// Ceiling on that history, so a very slow envelope cannot make a single
/// property evaluation walk an unbounded number of steps.
inline constexpr double kEnvelopeMaxWarmupSeconds = 2.0;

}  // namespace spectra
