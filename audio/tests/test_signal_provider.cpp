#include "wav_fixture.hpp"

#include <spectra/audio/signal_provider.hpp>
#include <spectra/core/driver.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

using namespace spectra;
using namespace spectra::audio;
using namespace spectra::audio::test;

namespace {

std::shared_ptr<const AnalysisCache> analyseTone(double frequency = 1000.0, double seconds = 2.0) {
    const TempDir dir;
    const auto path = dir.file("tone.wav");
    writeWav(path, makeSine(frequency, seconds, 44100, 1), WavSampleType::Float32);

    Result<AnalysisCache> cache = analyzeFile(
        path,
        AnalysisParams{.sampleRate = 44100, .rate = 100.0, .windowSize = 1024, .bandCount = 64});
    REQUIRE_MESSAGE(cache, cache.error());
    return std::make_shared<const AnalysisCache>(std::move(cache.value()));
}

}  // namespace

TEST_CASE("the documented vocabulary is served") {
    const AnalysisSignalProvider signals(analyseTone());

    CHECK(signals.sample(SignalRef{.name = "constant", .argA = 0.75f}, 1.0) ==
          doctest::Approx(0.75f));
    CHECK(signals.sample(SignalRef{.name = "mid"}, 1.0) > 0.0f);
    CHECK(signals.sample(SignalRef{.name = "rms"}, 1.0) > 0.0f);
    CHECK(signals.sample(SignalRef{.name = "peak"}, 1.0) > 0.0f);
    CHECK(signals.sample(SignalRef{.name = "band", .argA = 30.0f}, 1.0) >= 0.0f);
    CHECK(signals.sample(SignalRef{.name = "bandRange", .argA = 500.0f, .argB = 2000.0f}, 1.0) >
          0.0f);
    CHECK(signals.sample(SignalRef{.name = "peakBand"}, 1.0) > 0.0f);
    CHECK(signals.sample(SignalRef{.name = "trackPos"}, 1.0) ==
          doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("the stub's names still work") {
    // A scene authored against StubSignalProvider in Stage 1 must not break
    // when the real analysis replaces it in Stage 4.
    const AnalysisSignalProvider signals(analyseTone(8000.0));

    CHECK(signals.sample(SignalRef{.name = "treble"}, 1.0) ==
          signals.sample(SignalRef{.name = "high"}, 1.0));
    CHECK(signals.sample(SignalRef{.name = "level"}, 1.0) ==
          signals.sample(SignalRef{.name = "rms"}, 1.0));
}

TEST_CASE("an unknown signal is zero, not an error") {
    // A project referencing a signal this build does not have should still
    // load and render, just without that reaction.
    const AnalysisSignalProvider signals(analyseTone());

    CHECK(signals.sample(SignalRef{.name = "spectralCentroid"}, 1.0) == 0.0f);
    CHECK(signals.sample(SignalRef{.name = ""}, 1.0) == 0.0f);
}

TEST_CASE("trackPos runs 0 to 1 and clamps outside the track") {
    const AnalysisSignalProvider signals(analyseTone(1000.0, 4.0));

    CHECK(signals.sample(SignalRef{.name = "trackPos"}, 0.0) == doctest::Approx(0.0f));
    CHECK(signals.sample(SignalRef{.name = "trackPos"}, 2.0) ==
          doctest::Approx(0.5f).epsilon(0.01));
    CHECK(signals.sample(SignalRef{.name = "trackPos"}, 400.0) == doctest::Approx(1.0f));
    CHECK(signals.sample(SignalRef{.name = "trackPos"}, -10.0) == doctest::Approx(0.0f));
}

TEST_CASE("noise is smooth, bounded, and the same every time") {
    const AnalysisSignalProvider signals(analyseTone());
    const SignalRef ref{.name = "noise", .argA = 7.0f, .argB = 4.0f};

    float previous = signals.sample(ref, 0.0);
    for (int step = 1; step < 400; ++step) {
        const Seconds t = step * 0.005;
        const float value = signals.sample(ref, t);
        CAPTURE(t);

        REQUIRE(value >= 0.0f);
        REQUIRE(value <= 1.0f);

        // Smooth: at 4 Hz and 5 ms steps, no single step can jump far.
        REQUIRE(std::abs(value - previous) < 0.1f);
        previous = value;
    }

    CHECK(signals.sample(ref, 1.234) == signals.sample(ref, 1.234));

    // A different seed is a different sequence.
    CHECK(signals.sample(ref, 1.0) !=
          signals.sample(SignalRef{.name = "noise", .argA = 8.0f, .argB = 4.0f}, 1.0));
}

TEST_CASE("noise works on a track with no analysis behind it") {
    // It is the one signal that is not audio-derived, and the one that has to
    // keep working when nothing has been loaded yet.
    AnalysisParams params{.sampleRate = 44100, .rate = 100.0, .windowSize = 1024, .bandCount = 16};
    Result<AnalysisCache> empty =
        analyze(AudioBuffer{.samples = {}, .sampleRate = 44100, .channels = 1}, params);
    REQUIRE(empty);

    const AnalysisSignalProvider signals(
        std::make_shared<const AnalysisCache>(std::move(empty.value())));

    CHECK(signals.sample(SignalRef{.name = "constant", .argA = 0.5f}, 1.0) ==
          doctest::Approx(0.5f));
    CHECK(signals.sample(SignalRef{.name = "bass"}, 1.0) == 0.0f);
    CHECK(signals.beatIndex(1.0) == -1);
}

TEST_CASE("sampling is a pure function of time, in any order") {
    // The contract core/signal.hpp states, checked the way it would actually
    // break: preview walks time forwards, export walks a virtual clock, and
    // scrubbing walks it backwards.
    const AnalysisSignalProvider signals(analyseTone());

    const std::vector<std::string> names{
        "bass", "mid", "high", "rms", "beat", "beatPulse", "beatPhase", "peakBand", "trackPos"};
    std::vector<Seconds> times;
    for (int step = 0; step < 200; ++step) {
        times.push_back(step * 0.01);
    }

    std::vector<float> forwards;
    for (const Seconds t : times) {
        for (const std::string& name : names) {
            forwards.push_back(signals.sample(SignalRef{.name = name}, t));
        }
    }

    std::vector<Seconds> shuffled = times;
    std::mt19937 generator(12345);
    std::shuffle(shuffled.begin(), shuffled.end(), generator);

    std::vector<float> scattered(forwards.size());
    for (const Seconds t : shuffled) {
        const auto index = static_cast<std::size_t>(std::llround(t * 100.0));
        for (std::size_t name = 0; name < names.size(); ++name) {
            scattered[index * names.size() + name] =
                signals.sample(SignalRef{.name = names[name]}, t);
        }
    }

    CHECK(forwards == scattered);
}

TEST_CASE("a driver built on core drives a property from real audio") {
    // The seam itself: core's Driver, core's shaping chain, and an analysis
    // it knows nothing about.
    const auto cache = analyseTone(80.0);
    const AnalysisSignalProvider signals(cache);

    Driver driver;
    driver.signal = SignalRef{.name = "bass"};
    driver.amount = 2.0f;
    driver.blend = BlendOp::Add;

    const float base = 1.0f;
    const float driven = driver.applyTo(base, signals, 1.0);

    CHECK(driven > base);
    CHECK(driven == driver.applyTo(base, signals, 1.0));
}
