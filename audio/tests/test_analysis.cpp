#include "wav_fixture.hpp"

#include <spectra/audio/analysis.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace spectra;
using namespace spectra::audio;
using namespace spectra::audio::test;

namespace {

constexpr int kRate = 44100;

/// Small enough that the whole suite stays under a second, large enough that
/// the envelope and the normalization statistics have something to work with.
AnalysisParams fastParams() {
    return AnalysisParams{
        .sampleRate = kRate,
        .rate = 100.0,
        .windowSize = 1024,
        .bandCount = 64,
    };
}

AudioBuffer
mix(const std::vector<std::pair<double, float>>& partials, double seconds, int sampleRate = kRate) {
    AudioBuffer buffer{
        .samples = {},
        .sampleRate = sampleRate,
        .channels = 1,
    };
    const auto frames = static_cast<std::size_t>(seconds * sampleRate);
    buffer.samples.assign(frames, 0.0f);
    for (const auto& [frequency, amplitude] : partials) {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double phase =
                2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
            buffer.samples[frame] += amplitude * static_cast<float>(std::sin(phase));
        }
    }
    return buffer;
}

/// Index of the band whose range contains `hz`.
int bandContaining(const BandLayout& layout, float hz) {
    for (int band = 0; band < layout.count(); ++band) {
        if (hz >= layout.edges[static_cast<std::size_t>(band)] &&
            hz < layout.edges[static_cast<std::size_t>(band) + 1]) {
            return band;
        }
    }
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

TEST_CASE("bad parameters are rejected with a reason") {
    CHECK(AnalysisParams{}.validate());

    CHECK_FALSE(AnalysisParams{.sampleRate = 0}.validate());
    CHECK_FALSE(AnalysisParams{.rate = 0.0}.validate());
    CHECK_FALSE(AnalysisParams{.windowSize = 1000}.validate());
    CHECK_FALSE(AnalysisParams{.bandCount = 0}.validate());
    CHECK_FALSE(AnalysisParams{.minHz = 5000.0f, .maxHz = 100.0f}.validate());
    CHECK_FALSE(AnalysisParams{.smoothingRadius = -1}.validate());

    // 20 kHz does not exist in an 8 kHz file, and silently analysing an empty
    // top half would look like a very quiet recording.
    CHECK_FALSE(AnalysisParams{.sampleRate = 8000}.validate());
}

TEST_CASE("the parameter hash covers what changes the output, and nothing else") {
    const AnalysisParams base;

    CHECK(base.hash() == AnalysisParams{}.hash());

    // Thread count cannot change the result, so including it would throw the
    // cache away whenever a project moves to a machine with a different core
    // count.
    CHECK(base.hash() == AnalysisParams{.threadCount = 7}.hash());

    CHECK(base.hash() != AnalysisParams{.bandCount = 128}.hash());
    CHECK(base.hash() != AnalysisParams{.windowSize = 4096}.hash());
    CHECK(base.hash() != AnalysisParams{.scale = BandScale::Linear}.hash());
    CHECK(base.hash() != AnalysisParams{.tilt = SpectralTilt::Flat}.hash());
    CHECK(base.hash() != AnalysisParams{.gain = 2.0f}.hash());
    CHECK(base.hash() != AnalysisParams{.bass = {.attack = 0.5}}.hash());
}

TEST_CASE("analysing at the wrong sample rate is an error, not a wrong answer") {
    // The failure this prevents is subtle: everything still runs, and every
    // frequency is off by the ratio of the rates.
    const AudioBuffer audio = mix({{440.0, 0.5f}}, 0.5, 48000);

    const Result<AnalysisCache> cache = analyze(audio, AnalysisParams{.sampleRate = 44100});

    REQUIRE_FALSE(cache);
    CHECK(cache.error().find("48000") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The signal lands where it should
// ---------------------------------------------------------------------------

TEST_CASE("a sine puts its energy in the band that contains it") {
    const AudioBuffer audio = mix({{1000.0, 0.5f}}, 1.0);
    const Result<AnalysisCache> cache = analyze(audio, fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    const int expected = bandContaining(cache.value().layout(), 1000.0f);
    REQUIRE(expected >= 0);

    // Half a second in, well past the attack ramp.
    const std::span<const float> bands =
        cache.value().bandsAtFrame(static_cast<int>(cache.value().frameIndexAt(0.5)));
    const auto loudest = static_cast<int>(
        std::distance(bands.begin(), std::max_element(bands.begin(), bands.end())));

    CHECK(loudest == expected);
    CHECK(cache.value().at(0.5).peakBand ==
          doctest::Approx(static_cast<float>(expected) / (cache.value().bandCount() - 1))
              .epsilon(0.02));
}

TEST_CASE("band group energies follow the frequency they are named after") {
    const auto groupsOf = [](double frequency) {
        const Result<AnalysisCache> cache = analyze(mix({{frequency, 0.5f}}, 1.0), fastParams());
        REQUIRE_MESSAGE(cache, cache.error());
        return cache.value().at(0.5);
    };

    const AnalysisSample low = groupsOf(80.0);
    CHECK(low.bass > low.mid);
    CHECK(low.bass > low.high);

    const AnalysisSample middle = groupsOf(1000.0);
    CHECK(middle.mid > middle.bass);
    CHECK(middle.mid > middle.high);

    const AnalysisSample top = groupsOf(8000.0);
    CHECK(top.high > top.bass);
    CHECK(top.high > top.mid);
}

TEST_CASE("silence produces zeros, not NaNs") {
    // docs/04-AUDIO-ENGINE.md calls dividing by zero energy the single most
    // likely bug in this subsystem, and normalization is exactly where it
    // would happen.
    AudioBuffer silence{
        .samples = std::vector<float>(kRate, 0.0f),
        .sampleRate = kRate,
        .channels = 1,
    };

    for (const Normalization mode :
         {Normalization::None, Normalization::Track, Normalization::PerBand}) {
        CAPTURE(static_cast<int>(mode));
        AnalysisParams params = fastParams();
        params.normalization = mode;

        const Result<AnalysisCache> cache = analyze(silence, params);
        REQUIRE_MESSAGE(cache, cache.error());

        for (int frame = 0; frame < cache.value().frameCount(); ++frame) {
            for (const float value : cache.value().bandsAtFrame(frame)) {
                REQUIRE(std::isfinite(value));
                REQUIRE(value == doctest::Approx(0.0f));
            }
        }

        const AnalysisSample sample = cache.value().at(0.5);
        CHECK(sample.rms == doctest::Approx(0.0f));
        CHECK(sample.bass == doctest::Approx(0.0f));
        CHECK(sample.flux == doctest::Approx(0.0f));
    }
}

TEST_CASE("an empty track analyses to an empty cache") {
    const AudioBuffer nothing{.samples = {}, .sampleRate = kRate, .channels = 1};

    const Result<AnalysisCache> cache = analyze(nothing, fastParams());

    REQUIRE_MESSAGE(cache, cache.error());
    CHECK(cache.value().empty());
    CHECK(cache.value().frameCount() == 0);
    CHECK(cache.value().band(0, 0.0) == 0.0f);
    CHECK(cache.value().at(1.0).rms == 0.0f);
    CHECK(cache.value().bandsAtFrame(0).empty());
}

// ---------------------------------------------------------------------------
// Determinism — the point of the whole stage
// ---------------------------------------------------------------------------

TEST_CASE("the result does not depend on the thread count") {
    // docs/15-BUILD-ORDER.md names this as the Stage 3 exit criterion, and
    // chunked parallel analysis is exactly where it would break. Ours cannot:
    // the parallel phase is a pure function of one frame's samples, and
    // everything order-dependent runs afterwards on one thread.
    const AudioBuffer audio = mix({{220.0, 0.4f}, {1320.0, 0.2f}, {7000.0, 0.1f}}, 2.0);

    AnalysisParams params = fastParams();
    params.threadCount = 1;
    const Result<AnalysisCache> single = analyze(audio, params);
    REQUIRE_MESSAGE(single, single.error());

    for (const int threads : {2, 3, 5, 8, 13}) {
        CAPTURE(threads);
        params.threadCount = threads;
        const Result<AnalysisCache> parallel = analyze(audio, params);
        REQUIRE_MESSAGE(parallel, parallel.error());

        REQUIRE(parallel.value().frameCount() == single.value().frameCount());
        for (int frame = 0; frame < single.value().frameCount(); ++frame) {
            const std::span<const float> expected = single.value().bandsAtFrame(frame);
            const std::span<const float> actual = parallel.value().bandsAtFrame(frame);
            for (std::size_t band = 0; band < expected.size(); ++band) {
                // Bit-identical, not approximately equal. Anything less and
                // preview and export can disagree.
                REQUIRE(actual[band] == expected[band]);
            }
            REQUIRE(parallel.value().summaryAtFrame(frame).flux ==
                    single.value().summaryAtFrame(frame).flux);
            REQUIRE(parallel.value().summaryAtFrame(frame).rms ==
                    single.value().summaryAtFrame(frame).rms);
        }
    }
}

TEST_CASE("analysing the same audio twice gives the same cache") {
    const AudioBuffer audio = mix({{440.0, 0.5f}}, 1.0);

    const Result<AnalysisCache> first = analyze(audio, fastParams());
    const Result<AnalysisCache> second = analyze(audio, fastParams());
    REQUIRE(first);
    REQUIRE(second);

    for (int frame = 0; frame < first.value().frameCount(); ++frame) {
        const std::span<const float> a = first.value().bandsAtFrame(frame);
        const std::span<const float> b = second.value().bandsAtFrame(frame);
        REQUIRE(std::equal(a.begin(), a.end(), b.begin()));
    }
}

TEST_CASE("querying is a pure function of time") {
    // The requirement core's SignalProvider documents: sampling the same time
    // twice must give the same answer, in any order, from any thread.
    const Result<AnalysisCache> cache = analyze(mix({{440.0, 0.5f}}, 1.0), fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    const std::vector<Seconds> forwards{0.1, 0.25, 0.5, 0.75, 0.9};
    std::vector<float> ascending;
    for (const Seconds t : forwards) {
        ascending.push_back(cache.value().band(10, t));
    }

    std::vector<float> descending;
    for (auto it = forwards.rbegin(); it != forwards.rend(); ++it) {
        descending.push_back(cache.value().band(10, *it));
    }
    std::reverse(descending.begin(), descending.end());

    CHECK(ascending == descending);
}

// ---------------------------------------------------------------------------
// Time and interpolation
// ---------------------------------------------------------------------------

TEST_CASE("queries outside the track clamp to its ends") {
    const Result<AnalysisCache> cache = analyze(mix({{440.0, 0.5f}}, 0.5), fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    CHECK(cache.value().band(10, -5.0) == cache.value().band(10, 0.0));
    CHECK(cache.value().band(10, 100.0) ==
          cache.value().bandsAtFrame(cache.value().frameCount() - 1)[10]);
    CHECK(cache.value().band(-1, 0.25) == 0.0f);
    CHECK(cache.value().band(99999, 0.25) == 0.0f);
}

TEST_CASE("a value between frames is interpolated, not snapped") {
    const Result<AnalysisCache> cache = analyze(mix({{440.0, 0.5f}}, 1.0), fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    // Halfway between frames 30 and 31, at a 100 Hz analysis rate.
    const float before = cache.value().bandsAtFrame(30)[12];
    const float after = cache.value().bandsAtFrame(31)[12];
    const float between = cache.value().band(12, 30.5 / cache.value().rate());

    CHECK(between == doctest::Approx(0.5f * (before + after)));
}

TEST_CASE("the analysis rate is well above any project frame rate") {
    // The reason for 200 Hz: a 60fps preview interpolates a denser signal
    // rather than extrapolating a sparser one.
    const AnalysisParams params;
    CHECK(params.rate >= 120.0);
}

TEST_CASE("bandRange averages the bands it covers and never returns nothing") {
    const Result<AnalysisCache> cache = analyze(mix({{1000.0, 0.5f}}, 1.0), fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    CHECK(cache.value().bandRange(500.0f, 2000.0f, 0.5) >
          cache.value().bandRange(5000.0f, 10000.0f, 0.5));
    CHECK(cache.value().bandRange(2000.0f, 500.0f, 0.5) == 0.0f);

    // Narrower than one band of a 64-band layout. Returning zero here would
    // leave a driver silently dead rather than merely imprecise.
    CHECK(cache.value().bandRange(995.0f, 1005.0f, 0.5) > 0.0f);
}

// ---------------------------------------------------------------------------
// Normalization
// ---------------------------------------------------------------------------

TEST_CASE("track normalization rescales a quiet track without reshaping it") {
    AnalysisParams params = fastParams();
    params.normalization = Normalization::Track;

    const Result<AnalysisCache> loud = analyze(mix({{500.0, 0.8f}, {2000.0, 0.2f}}, 1.0), params);
    const Result<AnalysisCache> quiet =
        analyze(mix({{500.0, 0.08f}, {2000.0, 0.02f}}, 1.0), params);
    REQUIRE(loud);
    REQUIRE(quiet);

    // A master ten times quieter must not produce bars ten times shorter.
    for (int band = 0; band < params.bandCount; ++band) {
        CAPTURE(band);
        REQUIRE(quiet.value().band(band, 0.5) ==
                doctest::Approx(loud.value().band(band, 0.5)).epsilon(0.02));
    }

    // And the 4:1 ratio between the two partials has to survive, because
    // that is the spectral shape.
    const int lowBand = bandContaining(loud.value().layout(), 500.0f);
    const int highBand = bandContaining(loud.value().layout(), 2000.0f);
    REQUIRE(lowBand >= 0);
    REQUIRE(highBand >= 0);
    CHECK(loud.value().band(lowBand, 0.5) > loud.value().band(highBand, 0.5));
}

TEST_CASE("per-band normalization flattens the spectrum, which is the point") {
    AnalysisParams params = fastParams();
    params.normalization = Normalization::PerBand;

    const Result<AnalysisCache> cache = analyze(mix({{500.0, 0.8f}, {2000.0, 0.05f}}, 1.0), params);
    REQUIRE_MESSAGE(cache, cache.error());

    const int lowBand = bandContaining(cache.value().layout(), 500.0f);
    const int highBand = bandContaining(cache.value().layout(), 2000.0f);
    REQUIRE(lowBand >= 0);
    REQUIRE(highBand >= 0);

    // Sixteen times the amplitude in one partial, and both bands still reach
    // roughly the same height, because each is scaled against its own loudest
    // moment on this track.
    CHECK(cache.value().band(lowBand, 0.5) ==
          doctest::Approx(cache.value().band(highBand, 0.5)).epsilon(0.1));
}

TEST_CASE("gain is applied after normalization") {
    AnalysisParams params = fastParams();
    const Result<AnalysisCache> unity = analyze(mix({{440.0, 0.5f}}, 1.0), params);

    params.gain = 2.0f;
    const Result<AnalysisCache> doubled = analyze(mix({{440.0, 0.5f}}, 1.0), params);

    REQUIRE(unity);
    REQUIRE(doubled);
    CHECK(doubled.value().band(10, 0.5) == doctest::Approx(2.0f * unity.value().band(10, 0.5)));
}

// ---------------------------------------------------------------------------
// Envelopes and flux
// ---------------------------------------------------------------------------

TEST_CASE("bands rise fast and fall slowly") {
    // A second of tone followed by a second of silence. The characteristic
    // that separates a good-looking visualizer from a twitchy one.
    AudioBuffer audio = mix({{1000.0, 0.5f}}, 1.0);
    audio.samples.resize(audio.samples.size() * 2, 0.0f);

    AnalysisParams params = fastParams();
    params.normalization = Normalization::None;
    const Result<AnalysisCache> cache = analyze(audio, params);
    REQUIRE_MESSAGE(cache, cache.error());

    const int band = bandContaining(cache.value().layout(), 1000.0f);
    REQUIRE(band >= 0);

    const float steady = cache.value().band(band, 0.9);
    CHECK(steady > 0.0f);

    // Up within a few frames of the start.
    CHECK(cache.value().band(band, 0.05) > 0.5f * steady);

    // Still visibly falling 50 ms after the tone stops, and gone by half a
    // second — the default mid-band decay is 120 ms.
    CHECK(cache.value().band(band, 1.05) > 0.1f * steady);
    CHECK(cache.value().band(band, 1.5) < 0.05f * steady);
}

TEST_CASE("bass falls faster than treble") {
    // The third cause of the Avee bass bug: with one global decay the bass
    // band's sustained energy never falls.
    const AnalysisParams params;
    CHECK(params.bass.decay < params.mid.decay);
    CHECK(params.mid.decay < params.high.decay);
}

TEST_CASE("flux spikes when something starts and is quiet when nothing changes") {
    // Half a second of silence, then a tone.
    AudioBuffer audio{
        .samples = std::vector<float>(kRate / 2, 0.0f), .sampleRate = kRate, .channels = 1};
    const AudioBuffer tone = mix({{1000.0, 0.5f}}, 1.0);
    audio.samples.insert(audio.samples.end(), tone.samples.begin(), tone.samples.end());

    const Result<AnalysisCache> cache = analyze(audio, fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    const float atOnset = cache.value().at(0.5).flux;
    const float duringTone = cache.value().at(1.2).flux;
    const float duringSilence = cache.value().at(0.25).flux;

    CHECK(duringSilence == doctest::Approx(0.0f));
    CHECK(atOnset > 10.0f * duringTone);
}

TEST_CASE("the first frame does not invent an onset") {
    // Measuring frame zero against an all-zero predecessor would put a beat
    // on the downbeat of every track, including ones that start in silence.
    const Result<AnalysisCache> cache = analyze(mix({{1000.0, 0.5f}}, 0.5), fastParams());
    REQUIRE_MESSAGE(cache, cache.error());

    CHECK(cache.value().summaryAtFrame(0).flux == 0.0f);
}

// ---------------------------------------------------------------------------
// End to end
// ---------------------------------------------------------------------------

TEST_CASE("analyzeFile decodes at the rate the analysis needs") {
    const TempDir dir;
    const auto path = dir.file("tone.wav");

    // Written at 48 kHz, analysed at 44.1 kHz, and the tone still has to come
    // out at 1 kHz.
    AnalysisParams params = fastParams();
    writeWav(path, makeSine(1000.0, 1.0, 48000, 2), WavSampleType::Float32);

    const Result<AnalysisCache> cache = analyzeFile(path, params);

    REQUIRE_MESSAGE(cache, cache.error());
    CHECK(cache.value().duration() == doctest::Approx(1.0).epsilon(0.01));

    const std::span<const float> bands = cache.value().bandsAtFrame(50);
    const auto loudest = static_cast<int>(
        std::distance(bands.begin(), std::max_element(bands.begin(), bands.end())));
    CHECK(loudest == bandContaining(cache.value().layout(), 1000.0f));
}

TEST_CASE("analyzeFile reports a missing file rather than analysing nothing") {
    const Result<AnalysisCache> cache = analyzeFile("/nonexistent/spectra/missing.wav");

    REQUIRE_FALSE(cache);
    CHECK_FALSE(cache.error().empty());
}
