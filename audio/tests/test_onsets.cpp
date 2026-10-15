#include <spectra/audio/analysis.hpp>
#include <spectra/audio/onsets.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace spectra;
using namespace spectra::audio;

namespace {

constexpr double kRate = 200.0;

/// A flux envelope with a spike every `interval` seconds: the shape a click
/// train produces, written directly so the detector can be tested without
/// synthesising audio for it.
std::vector<float>
spikeTrain(double seconds, double interval, float height, float floorLevel = 0.01f) {
    std::vector<float> flux(static_cast<std::size_t>(seconds * kRate), floorLevel);
    for (double t = interval; t < seconds; t += interval) {
        const auto index = static_cast<std::size_t>(std::llround(t * kRate));
        if (index + 1 < flux.size()) {
            // Three frames wide, like a real transient spread by the window.
            flux[index - 1] = height * 0.5f;
            flux[index] = height;
            flux[index + 1] = height * 0.4f;
        }
    }
    return flux;
}

double estimatedBpm(const std::vector<Beat>& beats) {
    if (beats.size() < 2) {
        return 0.0;
    }
    const Seconds span = beats.back().time - beats.front().time;
    return 60.0 * static_cast<double>(beats.size() - 1) / span;
}

}  // namespace

TEST_CASE("a click train is detected at its own tempo") {
    // docs/15-BUILD-ORDER.md names this as a Stage 3 test: a click train at a
    // known BPM must come back at that BPM.
    for (const double bpm : {90.0, 120.0, 128.0, 174.0}) {
        CAPTURE(bpm);
        const std::vector<float> flux = spikeTrain(10.0, 60.0 / bpm, 1.0f);

        const std::vector<Beat> beats = detectOnsets(flux, kRate);

        CHECK(estimatedBpm(beats) == doctest::Approx(bpm).epsilon(0.02));
    }
}

TEST_CASE("onsets land on the spikes, not around them") {
    const std::vector<float> flux = spikeTrain(4.0, 0.5, 1.0f);

    const std::vector<Beat> beats = detectOnsets(flux, kRate);

    REQUIRE(beats.size() >= 6);
    for (const Beat& beat : beats) {
        CAPTURE(beat.time);
        const double offset = std::fmod(beat.time + 0.25, 0.5) - 0.25;
        CHECK(std::abs(offset) < 1.5 / kRate);
    }
}

TEST_CASE("one hit produces one beat, not a run of them") {
    // Every frame on the rising edge of a transient is above the threshold.
    // Taking them all would report a roll on every kick, and the refractory
    // period alone would then keep the earliest rather than the loudest.
    std::vector<float> flux(static_cast<std::size_t>(2.0 * kRate), 0.01f);
    for (std::size_t index = 100; index < 110; ++index) {
        flux[index] = 0.1f * static_cast<float>(index - 99);
    }
    for (std::size_t index = 110; index < 130; ++index) {
        flux[index] = 1.0f - 0.04f * static_cast<float>(index - 110);
    }

    const std::vector<Beat> beats = detectOnsets(flux, kRate);

    REQUIRE(beats.size() == 1);
    CHECK(beats[0].time == doctest::Approx(109.0 / kRate).epsilon(0.02));
}

TEST_CASE("silence produces no beats") {
    const std::vector<float> silence(static_cast<std::size_t>(5.0 * kRate), 0.0f);

    CHECK(detectOnsets(silence, kRate).empty());
}

TEST_CASE("a noise floor is not a beat grid") {
    // The failure that makes a fade-out look like a drum solo: with a local
    // median near zero, any wobble clears the relative threshold.
    std::vector<float> flux(static_cast<std::size_t>(5.0 * kRate));
    for (std::size_t index = 0; index < flux.size(); ++index) {
        flux[index] = 1e-5f * static_cast<float>((index * 7919) % 13);
    }

    CHECK(detectOnsets(flux, kRate).empty());
}

TEST_CASE("the refractory period caps the tempo") {
    // Spikes every 20 ms, which is 3000 BPM. Nothing musical is that fast,
    // and accepting it would turn a buzzing synth into a beat grid.
    const std::vector<float> flux = spikeTrain(2.0, 0.02, 1.0f);

    const std::vector<Beat> beats = detectOnsets(flux, kRate, OnsetParams{.minimumInterval = 0.1});

    for (std::size_t index = 1; index < beats.size(); ++index) {
        CHECK(beats[index].time - beats[index - 1].time >= doctest::Approx(0.1).epsilon(0.05));
    }
}

TEST_CASE("sensitivity trades recall for precision") {
    std::vector<float> flux = spikeTrain(10.0, 0.5, 1.0f);
    // Quiet hits halfway between the loud ones: the off-beat hi-hats. Three
    // times the noise floor, so whether they count depends entirely on where
    // the threshold is put.
    for (double t = 0.25; t < 10.0; t += 0.5) {
        flux[static_cast<std::size_t>(t * kRate)] = 0.03f;
    }

    const std::size_t strict = detectOnsets(flux, kRate, OnsetParams{.sensitivity = 4.0f}).size();
    const std::size_t loose = detectOnsets(flux, kRate, OnsetParams{.sensitivity = 1.2f}).size();

    CHECK(loose > strict);
}

TEST_CASE("strength reflects how far above the threshold a hit stood") {
    std::vector<float> flux = spikeTrain(8.0, 1.0, 0.2f);
    flux[static_cast<std::size_t>(4.0 * kRate)] = 2.0f;

    const std::vector<Beat> beats = detectOnsets(flux, kRate);

    REQUIRE(beats.size() >= 4);
    const auto loudest =
        std::max_element(beats.begin(), beats.end(), [](const Beat& a, const Beat& b) {
            return a.strength < b.strength;
        });
    CHECK(loudest->time == doctest::Approx(4.0).epsilon(0.02));
}

TEST_CASE("degenerate input is handled rather than indexed into") {
    CHECK(detectOnsets({}, kRate).empty());
    CHECK(detectOnsets(std::vector<float>{1.0f, 1.0f}, kRate).empty());
    CHECK(detectOnsets(std::vector<float>(100, 1.0f), 0.0).empty());
}

TEST_CASE("detection is reproducible") {
    const std::vector<float> flux = spikeTrain(6.0, 0.4, 1.0f);

    const std::vector<Beat> first = detectOnsets(flux, kRate);
    const std::vector<Beat> second = detectOnsets(flux, kRate);

    REQUIRE(first.size() == second.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        CHECK(first[index].time == second[index].time);
        CHECK(first[index].strength == second[index].strength);
    }
}

// ---------------------------------------------------------------------------
// The beat signals the drivers read
// ---------------------------------------------------------------------------

namespace {

/// A real click train: 120 BPM of short noise bursts.
AudioBuffer clickTrack(double bpm, double seconds, int sampleRate) {
    AudioBuffer audio{
        .samples = std::vector<float>(static_cast<std::size_t>(seconds * sampleRate), 0.0f),
        .sampleRate = sampleRate,
        .channels = 1,
    };

    const auto period = static_cast<std::size_t>(60.0 / bpm * sampleRate);
    const auto clickLength = static_cast<std::size_t>(0.005 * sampleRate);
    std::uint32_t noise = 12345;
    for (std::size_t start = period; start + clickLength < audio.samples.size(); start += period) {
        for (std::size_t index = 0; index < clickLength; ++index) {
            noise = noise * 1664525u + 1013904223u;
            const float sample = static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
            const auto fade = 1.0f - static_cast<float>(index) / static_cast<float>(clickLength);
            audio.samples[start + index] = sample * fade * 0.7f;
        }
    }
    return audio;
}

}  // namespace

TEST_CASE("a real click train at 120 BPM is detected at 120 BPM") {
    const AnalysisParams params{
        .sampleRate = 44100, .rate = 200.0, .windowSize = 1024, .bandCount = 64};
    const Result<AnalysisCache> cache = analyze(clickTrack(120.0, 10.0, 44100), params);
    REQUIRE_MESSAGE(cache, cache.error());

    const std::span<const Beat> beats = cache.value().beats();
    REQUIRE(beats.size() >= 15);

    const Seconds span = beats.back().time - beats.front().time;
    const double bpm = 60.0 * static_cast<double>(beats.size() - 1) / span;
    CHECK(bpm == doctest::Approx(120.0).epsilon(0.03));
}

TEST_CASE("beat index counts from the first beat and is negative before it") {
    const AnalysisParams params{
        .sampleRate = 44100, .rate = 200.0, .windowSize = 1024, .bandCount = 64};
    const Result<AnalysisCache> cache = analyze(clickTrack(120.0, 6.0, 44100), params);
    REQUIRE_MESSAGE(cache, cache.error());

    const std::span<const Beat> beats = cache.value().beats();
    REQUIRE(beats.size() >= 4);

    CHECK(cache.value().beatIndexAt(beats[0].time - 0.1) == -1);
    CHECK(cache.value().beatIndexAt(beats[0].time) == 0);
    CHECK(cache.value().beatIndexAt(beats[1].time - 0.01) == 0);
    CHECK(cache.value().beatIndexAt(beats[1].time) == 1);
    CHECK(cache.value().beatIndexAt(beats.back().time + 10.0) ==
          static_cast<std::int64_t>(beats.size()) - 1);
}

TEST_CASE("beat pulse spikes on the beat and decays between them") {
    const AnalysisParams params{
        .sampleRate = 44100, .rate = 200.0, .windowSize = 1024, .bandCount = 64, .pulseDecay = 0.2};
    const Result<AnalysisCache> cache = analyze(clickTrack(120.0, 6.0, 44100), params);
    REQUIRE_MESSAGE(cache, cache.error());

    const std::span<const Beat> beats = cache.value().beats();
    REQUIRE(beats.size() >= 3);

    const float onBeat = cache.value().beatPulseAt(beats[1].time);
    const float justAfter = cache.value().beatPulseAt(beats[1].time + 0.2);

    CHECK(onBeat > 0.0f);
    CHECK(justAfter == doctest::Approx(onBeat / std::exp(1.0f)).epsilon(0.01));
    CHECK(cache.value().beatPulseAt(beats[0].time - 0.5) == 0.0f);
}

TEST_CASE("beat phase ramps from zero to one across each beat") {
    // The signal Avee has no equivalent for, and the reason it cannot do
    // motion locked to the grid rather than merely twitching at it.
    const AnalysisParams params{
        .sampleRate = 44100, .rate = 200.0, .windowSize = 1024, .bandCount = 64};
    const Result<AnalysisCache> cache = analyze(clickTrack(120.0, 6.0, 44100), params);
    REQUIRE_MESSAGE(cache, cache.error());

    const std::span<const Beat> beats = cache.value().beats();
    REQUIRE(beats.size() >= 3);

    const Seconds start = beats[1].time;
    const Seconds interval = beats[2].time - start;

    CHECK(cache.value().beatPhaseAt(start) == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(cache.value().beatPhaseAt(start + interval * 0.25) ==
          doctest::Approx(0.25f).epsilon(0.02));
    CHECK(cache.value().beatPhaseAt(start + interval * 0.75) ==
          doctest::Approx(0.75f).epsilon(0.02));

    // Wraps rather than running past 1.
    CHECK(cache.value().beatPhaseAt(start + interval * 1.25) ==
          doctest::Approx(0.25f).epsilon(0.05));
}

TEST_CASE("a track with no beats still answers the beat signals") {
    const AnalysisParams params{
        .sampleRate = 44100, .rate = 100.0, .windowSize = 1024, .bandCount = 32};
    const AudioBuffer silence{
        .samples = std::vector<float>(44100, 0.0f), .sampleRate = 44100, .channels = 1};

    const Result<AnalysisCache> cache = analyze(silence, params);
    REQUIRE_MESSAGE(cache, cache.error());

    CHECK(cache.value().beats().empty());
    CHECK(cache.value().beatIndexAt(0.5) == -1);
    CHECK(cache.value().beatPulseAt(0.5) == 0.0f);
    CHECK(cache.value().beatPhaseAt(0.5) == 0.0f);
}
