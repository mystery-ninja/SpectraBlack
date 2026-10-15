#pragma once

#include <spectra/audio/analysis.hpp>
#include <spectra/core/signal.hpp>

#include <memory>

namespace spectra::audio {

/// Serves core's drivers from a finished analysis.
///
/// This is the seam between the two halves of the program: core knows that a
/// property can be driven by something called `bass` and knows nothing about
/// FFTs, and the audio module knows about FFTs and nothing about properties.
/// Stage 4 swaps `StubSignalProvider` for this one and the scene graph does
/// not notice.
///
/// The contract core states is that sampling is a pure function of time
/// (`core/signal.hpp`). This satisfies it the easy way: everything was
/// computed before the first query, so there is no state left to depend on.
///
/// Recognised names, from the Phase 1 vocabulary in
/// docs/04-AUDIO-ENGINE.md:
///
/// | Name | `argA` | `argB` | Meaning |
/// |---|---|---|---|
/// | `constant` | value | — | `argA`, unchanged |
/// | `rms`, `level` | — | — | Windowed loudness |
/// | `peak` | — | — | Peak amplitude |
/// | `bass`, `mid`, `high` | — | — | Band group energy |
/// | `treble` | — | — | Alias of `high` |
/// | `band` | index | — | One spectrum band |
/// | `bandRange` | low Hz | high Hz | Energy across a custom range |
/// | `beat` | — | — | Onset envelope, 0..1 |
/// | `beatPulse` | — | — | Decaying spike per detected beat |
/// | `beatIndex` | — | — | Beats elapsed, as a count |
/// | `beatPhase` | — | — | Position within the beat, 0..1 |
/// | `peakBand` | — | — | Loudest band, normalized |
/// | `trackPos` | — | — | Playhead position, 0..1 |
/// | `noise` | seed | Hz | Smooth random, not audio-driven |
///
/// An unknown name is zero rather than an error, so a project referencing a
/// signal this build does not have still loads and renders — just without
/// that reaction. `treble` and `level` are aliases because
/// `StubSignalProvider` used them, and a scene built against the stub should
/// not break when the real analysis arrives.
class AnalysisSignalProvider final : public SignalProvider {
public:
    /// Shared rather than referenced: the provider outlives the call that
    /// made it, and an evaluator holding a dangling analysis would fail
    /// somewhere far away from the mistake.
    explicit AnalysisSignalProvider(std::shared_ptr<const AnalysisCache> cache);

    [[nodiscard]] float sample(const SignalRef& ref, Seconds t) const override;

    [[nodiscard]] std::int64_t beatIndex(Seconds t) const override;

    [[nodiscard]] const AnalysisCache& cache() const { return *cache_; }

private:
    std::shared_ptr<const AnalysisCache> cache_;
};

}  // namespace spectra::audio
