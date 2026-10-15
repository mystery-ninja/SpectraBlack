#include <spectra/audio/dsp.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spectra::audio {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Periodic rather than symmetric: the sample at index `length` would repeat
/// index 0, so dividing by `length` is correct for spectral analysis where
/// the window is assumed to tile. Avee divides by `length - 1`, the symmetric
/// form, which is the right choice for filter design and the wrong one here.
/// The difference is one sample in 2048 and nobody would see it, but being
/// deliberate costs nothing.
double windowPhase(std::size_t index, std::size_t length) {
    return kTwoPi * static_cast<double>(index) / static_cast<double>(length);
}

float aWeightingRatio(double hz) {
    const double f2 = hz * hz;
    const double numerator = 1.4884e8 * f2 * f2;
    const double denominator =
        (f2 + 424.36) * (f2 + 1.4884e8) * std::sqrt((f2 + 11599.29) * (f2 + 544496.41));
    return denominator > 0.0 ? static_cast<float>(numerator / denominator) : 0.0f;
}

}  // namespace

void fillWindow(std::span<float> window, WindowShape shape) {
    const std::size_t length = window.size();
    for (std::size_t index = 0; index < length; ++index) {
        const double phase = windowPhase(index, length);
        double value = 0.0;
        switch (shape) {
            case WindowShape::Hann:
                value = 0.5 - 0.5 * std::cos(phase);
                break;
            case WindowShape::Hamming:
                value = 0.54 - 0.46 * std::cos(phase);
                break;
            case WindowShape::BlackmanHarris:
                value = 0.35875 - 0.48829 * std::cos(phase) + 0.14128 * std::cos(2.0 * phase) -
                        0.01168 * std::cos(3.0 * phase);
                break;
        }
        window[index] = static_cast<float>(value);
    }
}

float coherentGain(std::span<const float> window) {
    if (window.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const float value : window) {
        sum += static_cast<double>(value);
    }
    return static_cast<float>(sum / static_cast<double>(window.size()));
}

float hzToMel(float hz) {
    // O'Shaughnessy's form, the one everybody means by "the mel scale".
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float melToHz(float mel) {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

BandLayout makeBandLayout(int bandCount, float minHz, float maxHz, BandScale scale) {
    BandLayout layout;
    if (bandCount <= 0 || !(maxHz > minHz)) {
        return layout;
    }

    // A logarithmic layout cannot start at zero, and 20 Hz is both the bottom
    // of hearing and far below anything a speaker will reproduce, so clamping
    // here loses nothing real.
    const float low = scale == BandScale::Linear ? minHz : std::max(minHz, 1.0f);

    layout.edges.resize(static_cast<std::size_t>(bandCount) + 1);
    for (int edge = 0; edge <= bandCount; ++edge) {
        const auto fraction = static_cast<float>(edge) / static_cast<float>(bandCount);
        float hz = 0.0f;
        switch (scale) {
            case BandScale::Linear:
                hz = low + (maxHz - low) * fraction;
                break;
            case BandScale::Logarithmic:
                hz = low * std::pow(maxHz / low, fraction);
                break;
            case BandScale::Mel:
                hz = melToHz(hzToMel(low) + (hzToMel(maxHz) - hzToMel(low)) * fraction);
                break;
        }
        layout.edges[static_cast<std::size_t>(edge)] = hz;
    }
    return layout;
}

float BandLayout::centerHz(int band) const {
    if (band < 0 || band >= count()) {
        return 0.0f;
    }
    const float low = edges[static_cast<std::size_t>(band)];
    const float high = edges[static_cast<std::size_t>(band) + 1];
    return std::sqrt(low * high);
}

float BandLayout::widthHz(int band) const {
    if (band < 0 || band >= count()) {
        return 0.0f;
    }
    return edges[static_cast<std::size_t>(band) + 1] - edges[static_cast<std::size_t>(band)];
}

void mapBands(std::span<const float> magnitudes,
              double binWidthHz,
              const BandLayout& layout,
              std::span<float> out) {
    const int bands = layout.count();
    if (bands <= 0 || out.size() != static_cast<std::size_t>(bands) || magnitudes.empty() ||
        binWidthHz <= 0.0) {
        std::fill(out.begin(), out.end(), 0.0f);
        return;
    }

    const auto binCount = static_cast<double>(magnitudes.size());

    for (int band = 0; band < bands; ++band) {
        const double lowBin =
            static_cast<double>(layout.edges[static_cast<std::size_t>(band)]) / binWidthHz;
        const double highBin =
            static_cast<double>(layout.edges[static_cast<std::size_t>(band) + 1]) / binWidthHz;

        const auto firstBin = static_cast<std::int64_t>(std::ceil(lowBin));
        const auto lastBin = static_cast<std::int64_t>(std::floor(highBin));

        if (firstBin > lastBin || firstBin >= static_cast<std::int64_t>(binCount)) {
            // Narrower than a bin: read the spectrum at the band's centre.
            const double centre = std::clamp(0.5 * (lowBin + highBin), 0.0, binCount - 1.0);
            const auto left = static_cast<std::size_t>(centre);
            const std::size_t right = std::min(left + 1, magnitudes.size() - 1);
            const auto blend = static_cast<float>(centre - static_cast<double>(left));
            out[static_cast<std::size_t>(band)] =
                magnitudes[left] * (1.0f - blend) + magnitudes[right] * blend;
            continue;
        }

        const auto begin = static_cast<std::size_t>(std::max<std::int64_t>(firstBin, 0));
        const auto end =
            static_cast<std::size_t>(std::min<std::int64_t>(lastBin + 1, magnitudes.size()));

        float sum = 0.0f;
        for (std::size_t bin = begin; bin < end; ++bin) {
            sum += magnitudes[bin];
        }
        out[static_cast<std::size_t>(band)] =
            end > begin ? sum / static_cast<float>(end - begin) : 0.0f;
    }
}

std::vector<float> hammingKernel(int radius) {
    if (radius <= 0) {
        return {1.0f};
    }

    const std::size_t taps = static_cast<std::size_t>(2 * radius) + 1;
    std::vector<float> kernel(taps);
    double sum = 0.0;
    for (std::size_t tap = 0; tap < taps; ++tap) {
        const double phase = kTwoPi * static_cast<double>(tap) / static_cast<double>(taps - 1);
        const double value = 0.54 - 0.46 * std::cos(phase);
        kernel[tap] = static_cast<float>(value);
        sum += value;
    }

    const auto scale = static_cast<float>(1.0 / sum);
    for (float& value : kernel) {
        value *= scale;
    }
    return kernel;
}

void smoothAcrossBands(std::span<const float> in,
                       std::span<const float> kernel,
                       std::span<float> out) {
    if (in.size() != out.size() || kernel.empty()) {
        return;
    }

    const auto radius = static_cast<std::int64_t>(kernel.size() / 2);
    const auto size = static_cast<std::int64_t>(in.size());

    for (std::int64_t index = 0; index < size; ++index) {
        float sum = 0.0f;
        for (std::size_t tap = 0; tap < kernel.size(); ++tap) {
            const std::int64_t offset = index + static_cast<std::int64_t>(tap) - radius;
            const std::int64_t clamped = std::clamp<std::int64_t>(offset, 0, size - 1);
            sum += in[static_cast<std::size_t>(clamped)] * kernel[tap];
        }
        out[static_cast<std::size_t>(index)] = sum;
    }
}

EnvelopeCoefficients envelopeCoefficients(Seconds attackTime, Seconds decayTime, double rateHz) {
    const auto coefficient = [rateHz](Seconds time) {
        if (!(time > 0.0) || !(rateHz > 0.0)) {
            return 1.0f;
        }
        return static_cast<float>(1.0 - std::exp(-1.0 / (time * rateHz)));
    };

    return EnvelopeCoefficients{
        .attack = coefficient(attackTime),
        .decay = coefficient(decayTime),
    };
}

float followEnvelope(float current, float target, const EnvelopeCoefficients& coefficients) {
    const float rate = target > current ? coefficients.attack : coefficients.decay;
    return current + (target - current) * rate;
}

float tiltGain(SpectralTilt tilt, float hz) {
    constexpr float kReferenceHz = 1000.0f;

    switch (tilt) {
        case SpectralTilt::Flat:
            return 1.0f;

        case SpectralTilt::Pink:
            // Pink noise has power proportional to 1/f, so multiplying power
            // by f flattens it — and power scales as amplitude squared, which
            // is where the square root comes from. One octave up is a factor
            // of sqrt(2) in amplitude, which is the promised +3 dB.
            return hz > 0.0f ? std::sqrt(hz / kReferenceHz) : 0.0f;

        case SpectralTilt::Perceptual: {
            // A-weighting, but only below 1 kHz.
            //
            // The full curve also rolls the top octave off by several dB,
            // which is correct for a sound level meter and wrong for a
            // display: it dims the cymbals, which are the part people most
            // want to see moving. Avee reaches the same conclusion in
            // DspCurves.myAWeight1000, by the same clamp.
            const float clamped = std::min(hz, kReferenceHz);
            const float reference = aWeightingRatio(kReferenceHz);
            return reference > 0.0f ? aWeightingRatio(clamped) / reference : 1.0f;
        }
    }
    return 1.0f;
}

std::vector<float> tiltGains(SpectralTilt tilt, const BandLayout& layout) {
    std::vector<float> gains(static_cast<std::size_t>(std::max(layout.count(), 0)), 1.0f);
    for (int band = 0; band < layout.count(); ++band) {
        gains[static_cast<std::size_t>(band)] = tiltGain(tilt, layout.centerHz(band));
    }
    return gains;
}

}  // namespace spectra::audio
