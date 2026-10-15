#pragma once

#include <spectra/core/blend.hpp>
#include <spectra/core/shaping.hpp>
#include <spectra/core/signal.hpp>
#include <spectra/core/types.hpp>

namespace spectra {

/// An audio-reactive modifier on a property.
///
/// A property can carry several, applied in order, each on top of the last.
/// This is Avee's `MeasuredVar` decomposed into its parts: Avee bakes the
/// shaping into the measure *name* (`BeatCamShakeMore` versus
/// `BeatCamShakeLess`), where we keep the signal and its shaping separate so
/// any signal can be combined with any response.
struct Driver {
    SignalRef signal;
    ShapingChain shaping;
    float amount{1.0f};
    BlendOp blend{BlendOp::Add};
    bool enabled{true};

    /// The shaped signal before blending, for inspector readouts and tests.
    [[nodiscard]] float evaluateSignal(const SignalProvider& signals, Seconds t) const {
        return shaping.apply(signals, signal, t);
    }

    template<typename T>
    [[nodiscard]] T applyTo(const T& base, const SignalProvider& signals, Seconds t) const {
        if (!enabled || !signal.valid()) {
            return base;
        }
        return blendInto(blend, base, evaluateSignal(signals, t), amount);
    }
};

}  // namespace spectra
