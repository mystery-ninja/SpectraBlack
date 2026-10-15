#include <spectra/audio/signal_provider.hpp>

#include <cmath>

namespace spectra::audio {
namespace {

std::uint32_t mix(std::uint32_t value) {
    // A 32-bit avalanche. Any one input bit changing flips about half the
    // output bits, which is all a visual noise source needs.
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

float randomAt(std::int64_t step, std::uint32_t seed) {
    const auto hashed = mix(static_cast<std::uint32_t>(step) ^ mix(seed));
    return static_cast<float>(hashed) / static_cast<float>(0xFFFFFFFFU);
}

/// Value noise: random points at a fixed rate, smoothly joined.
///
/// A pure function of time and seed rather than a running generator, because
/// scrubbing backwards has to give the same answer as arriving forwards.
float smoothNoise(Seconds t, std::uint32_t seed, double hz) {
    const double position = t * hz;
    const auto step = static_cast<std::int64_t>(std::floor(position));
    const auto fraction = static_cast<float>(position - static_cast<double>(step));

    const float a = randomAt(step, seed);
    const float b = randomAt(step + 1, seed);

    // Smoothstep rather than a straight line, so the result has no corners
    // and does not read as a sequence of ramps.
    const float blend = fraction * fraction * (3.0f - 2.0f * fraction);
    return a + (b - a) * blend;
}

}  // namespace

AnalysisSignalProvider::AnalysisSignalProvider(std::shared_ptr<const AnalysisCache> cache)
    : cache_(std::move(cache)) {}

float AnalysisSignalProvider::sample(const SignalRef& ref, Seconds t) const {
    if (ref.name == "constant") {
        return ref.argA;
    }
    if (!cache_ || cache_->empty()) {
        return 0.0f;
    }

    // Not audio-derived, but constantly needed, and it is the one signal that
    // has to work on a track that has not been analysed.
    if (ref.name == "noise") {
        const double hz = ref.argB > 0.0f ? static_cast<double>(ref.argB) : 1.0;
        return smoothNoise(t, static_cast<std::uint32_t>(ref.argA), hz);
    }

    if (ref.name == "band") {
        return cache_->band(static_cast<int>(ref.argA), t);
    }
    if (ref.name == "bandRange") {
        return cache_->bandRange(ref.argA, ref.argB, t);
    }
    if (ref.name == "beatPulse") {
        return cache_->beatPulseAt(t);
    }
    if (ref.name == "beatPhase") {
        return cache_->beatPhaseAt(t);
    }
    if (ref.name == "beatIndex") {
        return static_cast<float>(cache_->beatIndexAt(t));
    }
    if (ref.name == "trackPos") {
        const Seconds duration = cache_->duration();
        return duration > 0.0 ? static_cast<float>(std::clamp(t / duration, 0.0, 1.0)) : 0.0f;
    }

    const AnalysisSample sample = cache_->at(t);
    if (ref.name == "rms" || ref.name == "level") {
        return sample.rms;
    }
    if (ref.name == "peak") {
        return sample.peak;
    }
    if (ref.name == "bass") {
        return sample.bass;
    }
    if (ref.name == "mid") {
        return sample.mid;
    }
    if (ref.name == "high" || ref.name == "treble") {
        return sample.high;
    }
    if (ref.name == "beat") {
        return sample.beat;
    }
    if (ref.name == "peakBand") {
        return sample.peakBand;
    }

    return 0.0f;
}

std::int64_t AnalysisSignalProvider::beatIndex(Seconds t) const {
    return cache_ ? cache_->beatIndexAt(t) : -1;
}

}  // namespace spectra::audio
