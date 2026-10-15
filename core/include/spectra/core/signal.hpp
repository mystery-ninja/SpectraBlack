#pragma once

#include <spectra/core/types.hpp>

#include <cstdint>
#include <string>

namespace spectra {

/// Names a signal to read from the audio analysis.
///
/// Modelled on Avee's `MeasuredVar`, which serializes a property's rule as
/// `"<measure> <argA> <argB>"` — see docs/09-AVEE-TEARDOWN.md. Keeping the
/// name as a string rather than an enum is what lets `.viz` import map Avee's
/// measure names onto ours, and lets Stage 3 add signals without touching core.
///
/// The string cost is paid once: the evaluator resolves a ref to a slot index
/// when the scene is compiled, not per frame.
struct SignalRef {
    std::string name;
    float argA{0.0f};  ///< e.g. band index, or lower frequency bound.
    float argB{0.0f};  ///< e.g. upper frequency bound.

    [[nodiscard]] bool valid() const { return !name.empty(); }
};

/// Read access to the analysed audio.
///
/// **Every implementation must be a pure function of time.** Sampling the same
/// `t` twice must give the same answer, in any order, from any thread. This is
/// not a nicety: preview renders frames in playback order and export renders
/// them on a virtual clock, and the two have to agree frame for frame. Avee's
/// analysis fails this — it smooths with a running per-frame state, so its
/// output depends on frame rate and on how you arrived at a time rather than
/// on the time itself. See docs/04-AUDIO-ENGINE.md.
class SignalProvider {
public:
    virtual ~SignalProvider() = default;

    SignalProvider() = default;
    SignalProvider(const SignalProvider&) = default;
    SignalProvider(SignalProvider&&) = default;
    SignalProvider& operator=(const SignalProvider&) = default;
    SignalProvider& operator=(SignalProvider&&) = default;

    /// Signal value at `t`, conventionally in [0, 1]. Unknown names yield 0 so
    /// that a project referencing a signal this build does not have still
    /// loads and renders, just without that reaction.
    [[nodiscard]] virtual float sample(const SignalRef& ref, Seconds t) const = 0;

    /// Index of the beat containing `t`, counting from 0 at the first beat and
    /// negative before it. Seeds the shaping chain's random stage, which is why
    /// it is an index rather than a time: a random value must be stable for the
    /// whole beat and must not change when you scrub back to it.
    [[nodiscard]] virtual std::int64_t beatIndex(Seconds t) const = 0;
};

/// Stand-in analysis for Stage 1, so the evaluator can be built and tested
/// before the audio engine exists. Produces smooth deterministic signals from
/// closed-form functions of time — no audio file, no FFT, no state.
///
/// Recognised names: `constant` (returns argA), `bass`, `mid`, `treble`,
/// `level`, `beatPulse`. Anything else returns 0.
class StubSignalProvider final : public SignalProvider {
public:
    explicit StubSignalProvider(double beatsPerMinute = 120.0);

    [[nodiscard]] float sample(const SignalRef& ref, Seconds t) const override;
    [[nodiscard]] std::int64_t beatIndex(Seconds t) const override;

    [[nodiscard]] double secondsPerBeat() const { return secondsPerBeat_; }

private:
    double secondsPerBeat_;
};

}  // namespace spectra
