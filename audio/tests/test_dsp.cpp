#include <spectra/audio/dsp.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace spectra;
using namespace spectra::audio;

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

TEST_CASE("Hann starts at zero and peaks in the middle") {
    std::vector<float> window(1024);
    fillWindow(window, WindowShape::Hann);

    CHECK(window.front() == doctest::Approx(0.0f));
    CHECK(window[512] == doctest::Approx(1.0f));

    // Periodic, not symmetric: the last sample is one step short of zero
    // rather than at it, because sample 1024 would repeat sample 0.
    CHECK(window.back() > 0.0f);
    CHECK(window.back() < 0.0001f);
}

TEST_CASE("coherent gain matches each window's DC term") {
    // These are the numbers that let a band value mean the same thing after
    // someone changes the window shape in the inspector.
    std::vector<float> window(2048);

    fillWindow(window, WindowShape::Hann);
    CHECK(coherentGain(window) == doctest::Approx(0.5f).epsilon(0.001));

    fillWindow(window, WindowShape::Hamming);
    CHECK(coherentGain(window) == doctest::Approx(0.54f).epsilon(0.001));

    fillWindow(window, WindowShape::BlackmanHarris);
    CHECK(coherentGain(window) == doctest::Approx(0.35875f).epsilon(0.001));
}

TEST_CASE("an empty window has no gain and does not divide by zero") {
    CHECK(coherentGain({}) == 0.0f);
}

// ---------------------------------------------------------------------------
// Band layout
// ---------------------------------------------------------------------------

TEST_CASE("the mel scale round-trips and matches its anchor") {
    // The mel scale is defined so that 1000 Hz is 1000 mel.
    CHECK(hzToMel(1000.0f) == doctest::Approx(1000.0f).epsilon(0.001));
    CHECK(hzToMel(0.0f) == doctest::Approx(0.0f));

    for (const float hz : {20.0f, 250.0f, 1000.0f, 4000.0f, 20000.0f}) {
        CAPTURE(hz);
        CHECK(melToHz(hzToMel(hz)) == doctest::Approx(hz).epsilon(0.0001));
    }
}

TEST_CASE("band layouts span the requested range and never go backwards") {
    for (const BandScale scale : {BandScale::Linear, BandScale::Logarithmic, BandScale::Mel}) {
        CAPTURE(static_cast<int>(scale));
        const BandLayout layout = makeBandLayout(128, 20.0f, 20000.0f, scale);

        REQUIRE(layout.count() == 128);
        CHECK(layout.edges.front() == doctest::Approx(20.0f));
        CHECK(layout.edges.back() == doctest::Approx(20000.0f));
        CHECK(std::is_sorted(layout.edges.begin(), layout.edges.end()));
    }
}

TEST_CASE("linear bands are equally wide and logarithmic bands equally wide in ratio") {
    const BandLayout linear = makeBandLayout(10, 0.0f, 1000.0f, BandScale::Linear);
    for (int band = 0; band < linear.count(); ++band) {
        CAPTURE(band);
        CHECK(linear.widthHz(band) == doctest::Approx(100.0f));
    }

    const BandLayout logarithmic = makeBandLayout(10, 20.0f, 20480.0f, BandScale::Logarithmic);
    const float ratio = logarithmic.edges[1] / logarithmic.edges[0];
    for (int band = 1; band < logarithmic.count(); ++band) {
        CAPTURE(band);
        const float thisRatio = logarithmic.edges[static_cast<std::size_t>(band) + 1] /
                                logarithmic.edges[static_cast<std::size_t>(band)];
        CHECK(thisRatio == doctest::Approx(ratio).epsilon(0.0001));
    }
}

TEST_CASE("mel sits between linear and logarithmic") {
    // The whole reason mel is the default: logarithmic spends too many bands
    // on sub-bass nobody can hear, linear spends almost none, mel is near
    // linear low down and near logarithmic high up.
    const int kMidBand = 64;
    const float linear = makeBandLayout(128, 20.0f, 20000.0f, BandScale::Linear).centerHz(kMidBand);
    const float mel = makeBandLayout(128, 20.0f, 20000.0f, BandScale::Mel).centerHz(kMidBand);
    const float logarithmic =
        makeBandLayout(128, 20.0f, 20000.0f, BandScale::Logarithmic).centerHz(kMidBand);

    CHECK(logarithmic < mel);
    CHECK(mel < linear);
}

TEST_CASE("a degenerate layout is empty rather than undefined") {
    CHECK(makeBandLayout(0, 20.0f, 20000.0f, BandScale::Mel).count() <= 0);
    CHECK(makeBandLayout(32, 20000.0f, 20.0f, BandScale::Mel).count() <= 0);
    CHECK(makeBandLayout(32, 100.0f, 100.0f, BandScale::Mel).count() <= 0);
}

TEST_CASE("band centres are geometric, not arithmetic") {
    const BandLayout layout = makeBandLayout(1, 20.0f, 80.0f, BandScale::Logarithmic);

    // Two octaves up from 20 Hz is 80 Hz, and the note in the middle is 40,
    // not 50.
    CHECK(layout.centerHz(0) == doctest::Approx(40.0f));
    CHECK(layout.centerHz(-1) == 0.0f);
    CHECK(layout.centerHz(7) == 0.0f);
}

// ---------------------------------------------------------------------------
// Band mapping
// ---------------------------------------------------------------------------

TEST_CASE("a spike maps into the band that contains it") {
    std::vector<float> magnitudes(1025, 0.0f);
    constexpr double kBinWidth = 48000.0 / 2048.0;  // 23.4375 Hz
    magnitudes[64] = 10.0f;                         // 1500 Hz

    const BandLayout layout = makeBandLayout(32, 20.0f, 20000.0f, BandScale::Logarithmic);
    std::vector<float> bands(static_cast<std::size_t>(layout.count()));
    mapBands(magnitudes, kBinWidth, layout, bands);

    const auto loudest = static_cast<int>(
        std::distance(bands.begin(), std::max_element(bands.begin(), bands.end())));

    CHECK(layout.edges[static_cast<std::size_t>(loudest)] <= 1500.0f);
    CHECK(layout.edges[static_cast<std::size_t>(loudest) + 1] >= 1500.0f);
}

TEST_CASE("bands narrower than a bin interpolate instead of reading zero") {
    // At 23.4 Hz per bin, the bottom thirty-odd bands of a 128-band log
    // layout are each narrower than one bin. Averaging "the bins inside the
    // band" finds none of them, and the display grows a comb of dead slots
    // that users read as missing bass.
    std::vector<float> magnitudes(1025, 2.0f);
    constexpr double kBinWidth = 48000.0 / 2048.0;

    const BandLayout layout = makeBandLayout(128, 20.0f, 20000.0f, BandScale::Logarithmic);
    std::vector<float> bands(static_cast<std::size_t>(layout.count()));
    mapBands(magnitudes, kBinWidth, layout, bands);

    for (int band = 0; band < layout.count(); ++band) {
        CAPTURE(band);
        CAPTURE(layout.centerHz(band));
        CHECK(bands[static_cast<std::size_t>(band)] == doctest::Approx(2.0f));
    }
}

TEST_CASE("mapping an empty or mismatched request yields silence") {
    const BandLayout layout = makeBandLayout(8, 20.0f, 20000.0f, BandScale::Mel);
    std::vector<float> bands(8, 5.0f);

    mapBands({}, 10.0, layout, bands);
    CHECK(std::all_of(bands.begin(), bands.end(), [](float value) { return value == 0.0f; }));

    const std::vector<float> magnitudes(100, 1.0f);
    bands.assign(8, 5.0f);
    mapBands(magnitudes, 0.0, layout, bands);
    CHECK(std::all_of(bands.begin(), bands.end(), [](float value) { return value == 0.0f; }));
}

// ---------------------------------------------------------------------------
// Smoothing across bands
// ---------------------------------------------------------------------------

TEST_CASE("the Hamming kernel is normalized and symmetric") {
    const std::vector<float> kernel = hammingKernel(3);

    REQUIRE(kernel.size() == 7);
    CHECK(std::accumulate(kernel.begin(), kernel.end(), 0.0f) == doctest::Approx(1.0f));
    CHECK(kernel[0] == doctest::Approx(kernel[6]));
    CHECK(kernel[2] == doctest::Approx(kernel[4]));
    CHECK(kernel[3] > kernel[2]);
}

TEST_CASE("a zero-radius kernel is the identity") {
    const std::vector<float> kernel = hammingKernel(0);

    REQUIRE(kernel.size() == 1);
    CHECK(kernel[0] == doctest::Approx(1.0f));
}

TEST_CASE("smoothing preserves a constant spectrum, including at the edges") {
    // Normalizing the kernel is what makes the first claim true, and clamping
    // rather than zero-padding is what makes the second true. Without either,
    // smoothing dims the display — and dims its ends more than its middle.
    const std::vector<float> in(16, 3.0f);
    const std::vector<float> kernel = hammingKernel(4);
    std::vector<float> out(in.size());

    smoothAcrossBands(in, kernel, out);

    for (const float value : out) {
        CHECK(value == doctest::Approx(3.0f));
    }
}

TEST_CASE("smoothing spreads a spike without changing its total") {
    std::vector<float> in(32, 0.0f);
    in[16] = 1.0f;
    const std::vector<float> kernel = hammingKernel(2);
    std::vector<float> out(in.size());

    smoothAcrossBands(in, kernel, out);

    CHECK(out[16] < 1.0f);
    CHECK(out[15] > 0.0f);
    CHECK(out[17] > 0.0f);
    CHECK(out[10] == doctest::Approx(0.0f));
    CHECK(std::accumulate(out.begin(), out.end(), 0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("the spectrum is not circular") {
    // Wrapping would put sub-bass energy into the treble bands, which is both
    // wrong and extremely visible.
    std::vector<float> in(32, 0.0f);
    in[0] = 1.0f;
    const std::vector<float> kernel = hammingKernel(2);
    std::vector<float> out(in.size());

    smoothAcrossBands(in, kernel, out);

    CHECK(out.back() == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------

TEST_CASE("envelope coefficients mean the same thing in seconds at any rate") {
    // The point of the fixed analysis rate. Avee's equivalents are per
    // rendered frame, so the same project decays differently on a slower
    // machine.
    constexpr double kFastRate = 200.0;
    constexpr double kSlowRate = 50.0;

    const EnvelopeCoefficients fast = envelopeCoefficients(0.1, 0.4, kFastRate);
    const EnvelopeCoefficients slow = envelopeCoefficients(0.1, 0.4, kSlowRate);

    const auto settle = [](float coefficient, double rate, Seconds duration) {
        float value = 0.0f;
        const auto steps = static_cast<int>(duration * rate);
        for (int step = 0; step < steps; ++step) {
            value += (1.0f - value) * coefficient;
        }
        return value;
    };

    // One time constant covers 1 - 1/e of the distance, whatever the rate.
    CHECK(settle(fast.attack, kFastRate, 0.1) == doctest::Approx(0.632f).epsilon(0.02));
    CHECK(settle(slow.attack, kSlowRate, 0.1) == doctest::Approx(0.632f).epsilon(0.02));
}

TEST_CASE("a zero time constant is instantaneous") {
    const EnvelopeCoefficients coefficients = envelopeCoefficients(0.0, 0.0, 200.0);

    CHECK(coefficients.attack == 1.0f);
    CHECK(coefficients.decay == 1.0f);
    CHECK(followEnvelope(0.0f, 1.0f, coefficients) == doctest::Approx(1.0f));
}

TEST_CASE("the follower rises fast and falls slow") {
    const EnvelopeCoefficients coefficients{.attack = 0.5f, .decay = 0.05f};

    CHECK(followEnvelope(0.0f, 1.0f, coefficients) == doctest::Approx(0.5f));
    CHECK(followEnvelope(1.0f, 0.0f, coefficients) == doctest::Approx(0.95f));
}

TEST_CASE("an impossible rate does not produce NaN coefficients") {
    const EnvelopeCoefficients coefficients = envelopeCoefficients(0.1, 0.1, 0.0);

    CHECK(coefficients.attack == 1.0f);
    CHECK(coefficients.decay == 1.0f);
}

// ---------------------------------------------------------------------------
// Spectral tilt
// ---------------------------------------------------------------------------

TEST_CASE("flat tilt changes nothing") {
    for (const float hz : {20.0f, 1000.0f, 20000.0f}) {
        CAPTURE(hz);
        CHECK(tiltGain(SpectralTilt::Flat, hz) == 1.0f);
    }
}

TEST_CASE("pink tilt is +3 dB per octave, anchored at 1 kHz") {
    CHECK(tiltGain(SpectralTilt::Pink, 1000.0f) == doctest::Approx(1.0f));

    const float atOneK = tiltGain(SpectralTilt::Pink, 1000.0f);
    const float atTwoK = tiltGain(SpectralTilt::Pink, 2000.0f);
    CHECK(atTwoK / atOneK == doctest::Approx(std::sqrt(2.0f)).epsilon(0.0001));

    // Ten octaves down from 1 kHz is about 1 Hz, and 3 dB an octave is 30 dB.
    CHECK(tiltGain(SpectralTilt::Pink, 10.0f) == doctest::Approx(0.1f).epsilon(0.0001));
    CHECK(tiltGain(SpectralTilt::Pink, 0.0f) == 0.0f);
}

TEST_CASE("perceptual tilt attenuates bass and leaves the top alone") {
    CHECK(tiltGain(SpectralTilt::Perceptual, 1000.0f) == doctest::Approx(1.0f));

    // A-weighting is roughly -30 dB at 50 Hz and -3 dB at 500 Hz.
    CHECK(tiltGain(SpectralTilt::Perceptual, 50.0f) == doctest::Approx(0.032f).epsilon(0.1));
    CHECK(tiltGain(SpectralTilt::Perceptual, 500.0f) == doctest::Approx(0.7f).epsilon(0.1));

    // The real A-weighting curve rolls the top octave off by several dB.
    // That is right for a sound level meter and wrong for a display: it dims
    // the cymbals, which is the part people most want to see move. Clamped,
    // so everything above 1 kHz is untouched.
    CHECK(tiltGain(SpectralTilt::Perceptual, 8000.0f) == doctest::Approx(1.0f));
    CHECK(tiltGain(SpectralTilt::Perceptual, 20000.0f) == doctest::Approx(1.0f));
}

TEST_CASE("pink tilt flattens a pink spectrum") {
    // The claim docs/04-AUDIO-ENGINE.md makes about the Avee bass bug, stated
    // as a test. Music is roughly pink — power proportional to 1/f — so raw
    // magnitude always shows bass dominating regardless of the mix.
    const BandLayout layout = makeBandLayout(48, 40.0f, 16000.0f, BandScale::Logarithmic);
    const std::vector<float> gains = tiltGains(SpectralTilt::Pink, layout);
    REQUIRE(gains.size() == 48);

    std::vector<float> untilted;
    std::vector<float> tilted;
    for (int band = 0; band < layout.count(); ++band) {
        // Pink noise: amplitude falls as 1/sqrt(f).
        const float amplitude = 1.0f / std::sqrt(layout.centerHz(band));
        untilted.push_back(amplitude);
        tilted.push_back(amplitude * gains[static_cast<std::size_t>(band)]);
    }

    const auto spread = [](const std::vector<float>& values) {
        const auto [low, high] = std::minmax_element(values.begin(), values.end());
        return *high / *low;
    };

    // Untilted, the bottom band reads twenty times the top one purely because
    // of how music is shaped. Tilted, they agree.
    CHECK(spread(untilted) > 15.0f);
    CHECK(spread(tilted) == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("tilt gains cover every band of a layout") {
    const BandLayout layout = makeBandLayout(16, 20.0f, 20000.0f, BandScale::Mel);

    CHECK(tiltGains(SpectralTilt::Flat, layout).size() == 16);
    CHECK(tiltGains(SpectralTilt::Pink, layout).size() == 16);
    CHECK(tiltGains(SpectralTilt::Perceptual, layout).size() == 16);
    CHECK(tiltGains(SpectralTilt::Pink, BandLayout{}).empty());
}
