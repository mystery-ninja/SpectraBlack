#include <spectra/audio/onsets.hpp>

#include <algorithm>
#include <cmath>

namespace spectra::audio {
namespace {

float medianOf(std::vector<float>& scratch) {
    if (scratch.empty()) {
        return 0.0f;
    }
    auto middle = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), middle, scratch.end());
    return *middle;
}

}  // namespace

std::vector<Beat>
detectOnsets(std::span<const float> flux, double rate, const OnsetParams& params) {
    std::vector<Beat> beats;
    if (flux.size() < 3 || !(rate > 0.0)) {
        return beats;
    }

    const auto count = static_cast<std::int64_t>(flux.size());
    const auto radius =
        std::max<std::int64_t>(1, static_cast<std::int64_t>(params.window * rate / 2.0));
    const auto refractory = static_cast<std::int64_t>(params.minimumInterval * rate);

    std::vector<float> scratch;
    scratch.reserve(static_cast<std::size_t>(2 * radius + 1));

    std::int64_t lastOnset = -refractory - 1;

    for (std::int64_t index = 1; index + 1 < count; ++index) {
        const float value = flux[static_cast<std::size_t>(index)];
        if (value < params.floor) {
            continue;
        }

        // A peak, not merely a frame above threshold. Without this the whole
        // rising edge of one hit reports as a run of onsets, and the
        // refractory period then picks whichever came first rather than the
        // loudest.
        const bool isPeak = value >= flux[static_cast<std::size_t>(index - 1)] &&
                            value > flux[static_cast<std::size_t>(index + 1)];
        if (!isPeak) {
            continue;
        }

        if (index - lastOnset <= refractory) {
            continue;
        }

        const std::int64_t first = std::max<std::int64_t>(index - radius, 0);
        const std::int64_t last = std::min<std::int64_t>(index + radius + 1, count);
        scratch.assign(flux.begin() + static_cast<std::ptrdiff_t>(first),
                       flux.begin() + static_cast<std::ptrdiff_t>(last));
        const float median = medianOf(scratch);

        // A passage of pure silence has a median of zero, and anything beats
        // zero. The floor above is what keeps that from turning dither into
        // a beat grid; this keeps the division finite.
        const float threshold = std::max(median * params.sensitivity, params.floor);
        if (value <= threshold) {
            continue;
        }

        beats.push_back(Beat{
            .time = static_cast<Seconds>(index) / rate,
            .strength = value / threshold,
        });
        lastOnset = index;
    }

    return beats;
}

}  // namespace spectra::audio
