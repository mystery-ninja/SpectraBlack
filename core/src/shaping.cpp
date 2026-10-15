#include <spectra/core/shaping.hpp>

#include <algorithm>
#include <cmath>

namespace spectra {
namespace {

/// splitmix32. Cheap, well-distributed, and — the part that matters here —
/// a pure function of its input, so the same beat always gives the same value.
std::uint32_t hashCombine(std::uint32_t seed, std::uint32_t value) {
    std::uint32_t x = seed + 0x9E3779B9u + value * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x21F0AAADu;
    x ^= x >> 15;
    x *= 0x735A2D97u;
    x ^= x >> 15;
    return x;
}

float unitRandom(std::uint32_t seed, std::int64_t beat) {
    const auto low = static_cast<std::uint32_t>(static_cast<std::uint64_t>(beat) & 0xFFFFFFFFu);
    const auto high = static_cast<std::uint32_t>(static_cast<std::uint64_t>(beat) >> 32);
    const std::uint32_t h = hashCombine(hashCombine(seed, low), high);
    return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
}

/// One step of a one-pole follower towards `target`.
float followTowards(float current, float target, double timeConstant, double dt) {
    if (timeConstant <= 0.0) {
        return target;
    }
    const auto coefficient = static_cast<float>(std::exp(-dt / timeConstant));
    return target + (current - target) * coefficient;
}

}  // namespace

float Remap::apply(float v) const {
    const float span = inMax - inMin;
    float u = span != 0.0f ? (v - inMin) / span : 0.0f;
    if (invert) {
        u = 1.0f - u;
    }
    return outMin + (outMax - outMin) * u;
}

float Curve::apply(float v) const {
    switch (kind) {
        case CurveKind::Linear:
            return v;
        case CurveKind::Exponential: {
            // Keep the sign so the curve still works on bipolar signals, and
            // guard the fractional-exponent-of-a-negative case.
            const float magnitude = std::pow(std::abs(v), std::max(exponent, 0.0f));
            return v < 0.0f ? -magnitude : magnitude;
        }
        case CurveKind::Smoothstep: {
            const float u = std::clamp(v, 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        }
    }
    return v;
}

float RandomStage::apply(float v, std::int64_t beat) const {
    if (!enabled) {
        return v;
    }
    const float r = unitRandom(seed, beat);
    return v * (1.0f + (r - 1.0f) * amount);
}

float ShapingChain::apply(const SignalProvider& signals, const SignalRef& ref, Seconds t) const {
    // The delay is applied by shifting the time we sample at rather than after
    // the envelope. Both stages are time-invariant, so delaying the input and
    // delaying the output are the same thing, and shifting first means the
    // envelope's history walk is already aligned to the delayed signal.
    const Seconds sampleTime = t - delaySeconds;

    float shaped = 0.0f;
    if (!envelope.active()) {
        shaped = applyStatic(signals.sample(ref, sampleTime));
    } else {
        // An envelope follower is a feedback loop, so its output at time t
        // depends on its own history. Carrying that state across frames is
        // what makes Avee's smoothing frame-rate dependent and non-repeatable,
        // which would break preview/export parity. Instead we rebuild the
        // state from scratch on every evaluation, walking a bounded window of
        // history at a fixed hop. That makes the result a pure function of t:
        // the same time gives the same answer no matter what was rendered
        // before it, at any frame rate, scrubbing in either direction.
        const double timeConstant = std::max(envelope.attack, envelope.decay);
        const double warmup =
            std::min(kEnvelopeWarmupTimeConstants * timeConstant, kEnvelopeMaxWarmupSeconds);

        const auto steps = static_cast<int>(std::ceil(warmup / kShapingHopSeconds));

        Seconds walk = sampleTime - static_cast<double>(steps) * kShapingHopSeconds;
        shaped = applyStatic(signals.sample(ref, walk));

        for (int i = 0; i < steps; ++i) {
            walk += kShapingHopSeconds;
            const float target = applyStatic(signals.sample(ref, walk));
            const double tau = target > shaped ? envelope.attack : envelope.decay;
            shaped = followTowards(shaped, target, tau, kShapingHopSeconds);
        }
    }

    return random.apply(shaped, signals.beatIndex(sampleTime));
}

}  // namespace spectra
