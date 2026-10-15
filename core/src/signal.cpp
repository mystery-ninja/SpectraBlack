#include <spectra/core/signal.hpp>

#include <algorithm>
#include <cmath>

namespace spectra {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// Sine mapped from [-1, 1] to [0, 1].
double unipolarSine(double hz, Seconds t, double phase = 0.0) {
    return 0.5 + 0.5 * std::sin(2.0 * kPi * hz * t + phase);
}

}  // namespace

StubSignalProvider::StubSignalProvider(double beatsPerMinute)
    : secondsPerBeat_(60.0 / std::max(beatsPerMinute, 1.0)) {}

float StubSignalProvider::sample(const SignalRef& ref, Seconds t) const {
    if (ref.name == "constant") {
        return ref.argA;
    }
    if (ref.name == "bass") {
        return static_cast<float>(unipolarSine(2.0, t));
    }
    if (ref.name == "mid") {
        return static_cast<float>(unipolarSine(5.0, t, 1.0));
    }
    if (ref.name == "treble") {
        return static_cast<float>(unipolarSine(11.0, t, 2.0));
    }
    if (ref.name == "level") {
        return static_cast<float>(0.5 + 0.25 * std::sin(2.0 * kPi * 0.5 * t));
    }
    if (ref.name == "beatPulse") {
        // Sharp attack on the beat decaying to zero before the next one, which
        // is the shape a kick actually has.
        const double sinceBeat = t - std::floor(t / secondsPerBeat_) * secondsPerBeat_;
        const double fraction = sinceBeat / secondsPerBeat_;
        return static_cast<float>(std::exp(-6.0 * fraction));
    }
    return 0.0f;
}

std::int64_t StubSignalProvider::beatIndex(Seconds t) const {
    return static_cast<std::int64_t>(std::floor(t / secondsPerBeat_));
}

}  // namespace spectra
