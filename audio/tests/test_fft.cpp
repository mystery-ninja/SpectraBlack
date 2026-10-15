#include <spectra/audio/dsp.hpp>
#include <spectra/audio/fft.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace spectra;
using namespace spectra::audio;

namespace {

constexpr int kSize = 2048;
constexpr double kSampleRate = 48000.0;

std::vector<float> sineAtBin(int bin, int size, float amplitude = 1.0f) {
    std::vector<float> samples(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index) {
        const double phase = 2.0 * std::numbers::pi * bin * index / size;
        samples[static_cast<std::size_t>(index)] = amplitude * static_cast<float>(std::sin(phase));
    }
    return samples;
}

std::size_t argMax(const std::vector<float>& values) {
    return static_cast<std::size_t>(
        std::distance(values.begin(), std::max_element(values.begin(), values.end())));
}

}  // namespace

TEST_CASE("PFFFT's size constraint is checked, not assumed") {
    CHECK(RealFft::isSupportedSize(2048));
    CHECK(RealFft::isSupportedSize(32));
    CHECK(RealFft::isSupportedSize(96));  // 2^5 * 3

    CHECK_FALSE(RealFft::isSupportedSize(2000));  // has a factor of 125 but only 2^4
    CHECK_FALSE(RealFft::isSupportedSize(48));    // 2^4 * 3, one power of two short
    CHECK_FALSE(RealFft::isSupportedSize(2044));  // 2^2 * 7 * 73
    CHECK_FALSE(RealFft::isSupportedSize(0));
    CHECK_FALSE(RealFft::isSupportedSize(-2048));
}

TEST_CASE("an unsupported size is an error rather than a crash") {
    // Window size is reachable from a project file, so a bad one has to come
    // back as a message rather than as undefined behaviour inside PFFFT.
    const Result<RealFft> fft = RealFft::create(1000);

    REQUIRE_FALSE(fft);
    CHECK_FALSE(fft.error().empty());
}

TEST_CASE("a sine lands in its own bin") {
    const Result<RealFft> fft = RealFft::create(kSize);
    REQUIRE_MESSAGE(fft, fft.error());

    constexpr int kBin = 64;
    const std::vector<float> samples = sineAtBin(kBin, kSize);

    std::vector<float> magnitudes(static_cast<std::size_t>(fft.value().binCount()));
    fft.value().transform(samples, magnitudes);

    CHECK(argMax(magnitudes) == kBin);

    // 64 bins at 23.4375 Hz each.
    CHECK(kBin * fft.value().binWidth(kSampleRate) == doctest::Approx(1500.0));
}

TEST_CASE("magnitudes have the scale the header promises") {
    const Result<RealFft> fft = RealFft::create(kSize);
    REQUIRE_MESSAGE(fft, fft.error());

    const std::vector<float> samples = sineAtBin(64, kSize, 1.0f);
    std::vector<float> magnitudes(static_cast<std::size_t>(fft.value().binCount()));
    fft.value().transform(samples, magnitudes);

    // An unwindowed full-scale sine sitting exactly on a bin puts N/2 there.
    CHECK(magnitudes[64] == doctest::Approx(kSize / 2.0).epsilon(0.001));
}

TEST_CASE("a Hann window splits a quarter of a bin's energy into each neighbour") {
    // Worth pinning down because it is the behaviour that makes across-band
    // smoothing look like a cosmetic choice rather than a necessity: even a
    // pure tone already occupies three bins before anyone smooths anything.
    const Result<RealFft> fft = RealFft::create(kSize);
    REQUIRE_MESSAGE(fft, fft.error());

    std::vector<float> window(kSize);
    fillWindow(window, WindowShape::Hann);

    std::vector<float> samples = sineAtBin(64, kSize);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] *= window[index];
    }

    std::vector<float> magnitudes(static_cast<std::size_t>(fft.value().binCount()));
    fft.value().transform(samples, magnitudes);

    CHECK(magnitudes[64] == doctest::Approx(kSize / 4.0).epsilon(0.001));
    CHECK(magnitudes[63] == doctest::Approx(kSize / 8.0).epsilon(0.001));
    CHECK(magnitudes[65] == doctest::Approx(kSize / 8.0).epsilon(0.001));
    CHECK(magnitudes[70] < 1.0f);
}

TEST_CASE("DC and Nyquist come out of the packed first bin") {
    const Result<RealFft> fft = RealFft::create(kSize);
    REQUIRE_MESSAGE(fft, fft.error());

    const auto bins = static_cast<std::size_t>(fft.value().binCount());
    std::vector<float> magnitudes(bins);

    SUBCASE("a constant signal is all DC") {
        const std::vector<float> samples(kSize, 0.5f);
        fft.value().transform(samples, magnitudes);

        CHECK(magnitudes[0] == doctest::Approx(kSize * 0.5).epsilon(0.001));
        CHECK(magnitudes[1] < 0.001f);
        CHECK(magnitudes[bins - 1] < 0.001f);
    }

    SUBCASE("an alternating signal is all Nyquist") {
        std::vector<float> samples(kSize);
        for (std::size_t index = 0; index < samples.size(); ++index) {
            samples[index] = index % 2 == 0 ? 1.0f : -1.0f;
        }
        fft.value().transform(samples, magnitudes);

        CHECK(magnitudes[bins - 1] == doctest::Approx(kSize).epsilon(0.001));
        CHECK(magnitudes[0] < 0.001f);
    }
}

TEST_CASE("silence produces zeros, not NaNs") {
    // The failure mode docs/04-AUDIO-ENGINE.md calls the single most likely
    // bug in the subsystem. It starts here and is checked again at every
    // later stage.
    const Result<RealFft> fft = RealFft::create(256);
    REQUIRE_MESSAGE(fft, fft.error());

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> magnitudes(static_cast<std::size_t>(fft.value().binCount()), -1.0f);
    fft.value().transform(silence, magnitudes);

    for (const float magnitude : magnitudes) {
        CHECK(magnitude == 0.0f);
    }
}

TEST_CASE("mismatched spans are rejected rather than overrunning") {
    const Result<RealFft> fft = RealFft::create(64);
    REQUIRE_MESSAGE(fft, fft.error());

    const std::vector<float> tooFew(32, 1.0f);
    std::vector<float> magnitudes(static_cast<std::size_t>(fft.value().binCount()), 7.0f);

    fft.value().transform(tooFew, magnitudes);

    CHECK(magnitudes[0] == 7.0f);
}

TEST_CASE("transforming the same input twice gives the same answer") {
    const Result<RealFft> fft = RealFft::create(512);
    REQUIRE_MESSAGE(fft, fft.error());

    const std::vector<float> samples = sineAtBin(17, 512, 0.3f);
    const auto bins = static_cast<std::size_t>(fft.value().binCount());

    std::vector<float> first(bins);
    std::vector<float> second(bins);
    fft.value().transform(samples, first);
    fft.value().transform(samples, second);

    CHECK(first == second);
}
