#include <spectra/audio/analysis.hpp>
#include <spectra/audio/decoder.hpp>
#include <spectra/audio/fft.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <type_traits>

namespace spectra::audio {
namespace {

/// Reads a window of mono samples, zero-padding past either end.
///
/// The window is centred on the frame's timestamp rather than starting at it.
/// Starting at it would make every value lag by half a window — 23 ms at the
/// default settings, which is a visible and entirely avoidable offset between
/// a kick and the bar that reacts to it.
void readWindow(std::span<const float> mono, std::int64_t centre, std::span<float> out) {
    const auto size = static_cast<std::int64_t>(out.size());
    const std::int64_t start = centre - size / 2;
    const auto available = static_cast<std::int64_t>(mono.size());

    for (std::int64_t index = 0; index < size; ++index) {
        const std::int64_t source = start + index;
        out[static_cast<std::size_t>(index)] =
            (source >= 0 && source < available) ? mono[static_cast<std::size_t>(source)] : 0.0f;
    }
}

/// Everything one worker needs. One of these per thread, so nothing is shared
/// and there is nothing to synchronize.
struct Worker {
    RealFft fft;
    std::vector<float> samples;
    std::vector<float> magnitudes;
    std::vector<float> rawBands;
    std::vector<float> smoothed;
};

float percentile(std::vector<float>& values, double fraction) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    auto nth = values.begin() + static_cast<std::ptrdiff_t>(index);
    std::nth_element(values.begin(), nth, values.end());
    return *nth;
}

/// Which envelope applies to a band, by where its centre falls.
const EnvelopeTimes& envelopeFor(const AnalysisParams& params, float centreHz) {
    if (centreHz < kBassMaxHz) {
        return params.bass;
    }
    if (centreHz < kMidMaxHz) {
        return params.mid;
    }
    return params.high;
}

std::uint64_t hashBytes(std::uint64_t seed, const void* data, std::size_t size) {
    // FNV-1a. Not cryptographic and does not need to be: it keys a cache that
    // is cheap to rebuild if it ever collides.
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        seed ^= bytes[index];
        seed *= 0x100000001b3ULL;
    }
    return seed;
}

/// Deliberately restricted to scalars.
///
/// Hashing a struct wholesale reads its padding, which is never initialised,
/// so two parameter sets that compare equal hash differently and the disk
/// cache never hits. Requiring one call per field makes that impossible
/// rather than merely unlikely.
template<typename T>
std::uint64_t hashValue(std::uint64_t seed, const T& value) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "hash fields, not structs: padding is not zeroed");
    return hashBytes(seed, &value, sizeof(value));
}

std::uint64_t hashEnvelope(std::uint64_t seed, const EnvelopeTimes& times) {
    seed = hashValue(seed, times.attack);
    return hashValue(seed, times.decay);
}

std::uint64_t hashOnsets(std::uint64_t seed, const OnsetParams& onsets) {
    seed = hashValue(seed, onsets.window);
    seed = hashValue(seed, onsets.sensitivity);
    seed = hashValue(seed, onsets.minimumInterval);
    return hashValue(seed, onsets.floor);
}

}  // namespace

Result<bool> AnalysisParams::validate() const {
    if (sampleRate <= 0) {
        return Result<bool>::error("analysis sample rate must be positive");
    }
    if (!(rate > 0.0)) {
        return Result<bool>::error("analysis rate must be positive");
    }
    if (!RealFft::isSupportedSize(windowSize)) {
        return Result<bool>::error("window size " + std::to_string(windowSize) +
                                   " is not a supported FFT size (try 1024, 2048, 4096)");
    }
    if (bandCount <= 0) {
        return Result<bool>::error("band count must be positive");
    }
    if (!(maxHz > minHz)) {
        return Result<bool>::error("the frequency range is empty");
    }
    if (maxHz > static_cast<float>(sampleRate) / 2.0f) {
        return Result<bool>::error("the frequency range reaches above Nyquist for " +
                                   std::to_string(sampleRate) + " Hz");
    }
    if (smoothingRadius < 0) {
        return Result<bool>::error("smoothing radius cannot be negative");
    }
    return Result<bool>::ok(true);
}

std::uint64_t AnalysisParams::hash() const {
    std::uint64_t seed = 0xcbf29ce484222325ULL;
    seed = hashValue(seed, sampleRate);
    seed = hashValue(seed, rate);
    seed = hashValue(seed, windowSize);
    seed = hashValue(seed, bandCount);
    seed = hashValue(seed, minHz);
    seed = hashValue(seed, maxHz);
    seed = hashValue(seed, scale);
    seed = hashValue(seed, window);
    seed = hashValue(seed, tilt);
    seed = hashValue(seed, smoothingRadius);
    seed = hashValue(seed, normalization);
    seed = hashValue(seed, gain);
    seed = hashEnvelope(seed, bass);
    seed = hashEnvelope(seed, mid);
    seed = hashEnvelope(seed, high);
    seed = hashOnsets(seed, onsets);
    seed = hashValue(seed, pulseDecay);
    // threadCount is deliberately absent. It cannot change the result, and
    // including it would throw the cache away when a project moves machines.
    return seed;
}

std::span<const float> AnalysisCache::bandsAtFrame(int frame) const {
    if (frameCount_ <= 0) {
        return {};
    }
    const int clamped = std::clamp(frame, 0, frameCount_ - 1);
    const auto offset =
        static_cast<std::size_t>(clamped) * static_cast<std::size_t>(params_.bandCount);
    return std::span<const float>(bands_).subspan(offset,
                                                  static_cast<std::size_t>(params_.bandCount));
}

const FrameSummary& AnalysisCache::summaryAtFrame(int frame) const {
    static const FrameSummary kSilence;
    if (frameCount_ <= 0) {
        return kSilence;
    }
    return summaries_[static_cast<std::size_t>(std::clamp(frame, 0, frameCount_ - 1))];
}

double AnalysisCache::frameIndexAt(Seconds t) const {
    if (frameCount_ <= 1) {
        return 0.0;
    }
    return std::clamp(t * params_.rate, 0.0, static_cast<double>(frameCount_ - 1));
}

float AnalysisCache::band(int index, Seconds t) const {
    if (frameCount_ <= 0 || index < 0 || index >= params_.bandCount) {
        return 0.0f;
    }

    const double position = frameIndexAt(t);
    const auto lower = static_cast<int>(position);
    const int upper = std::min(lower + 1, frameCount_ - 1);
    const auto blend = static_cast<float>(position - lower);

    const float a = bandsAtFrame(lower)[static_cast<std::size_t>(index)];
    const float b = bandsAtFrame(upper)[static_cast<std::size_t>(index)];
    return a + (b - a) * blend;
}

float AnalysisCache::bandRange(float lowHz, float highHz, Seconds t) const {
    if (frameCount_ <= 0 || !(highHz > lowHz)) {
        return 0.0f;
    }

    float sum = 0.0f;
    int counted = 0;
    for (int index = 0; index < params_.bandCount; ++index) {
        const float centre = layout_.centerHz(index);
        if (centre >= lowHz && centre < highHz) {
            sum += band(index, t);
            ++counted;
        }
    }

    if (counted > 0) {
        return sum / static_cast<float>(counted);
    }

    // The requested range is narrower than one band. Return the band that
    // contains it rather than zero, so a driver asking for a 50 Hz slice of a
    // 16-band layout still reacts to something.
    const float centre = 0.5f * (lowHz + highHz);
    for (int index = 0; index < params_.bandCount; ++index) {
        if (centre >= layout_.edges[static_cast<std::size_t>(index)] &&
            centre < layout_.edges[static_cast<std::size_t>(index) + 1]) {
            return band(index, t);
        }
    }
    return 0.0f;
}

AnalysisSample AnalysisCache::at(Seconds t) const {
    if (frameCount_ <= 0) {
        return {};
    }

    const double position = frameIndexAt(t);
    const auto lowerIndex = static_cast<int>(position);
    const int upperIndex = std::min(lowerIndex + 1, frameCount_ - 1);
    const auto blend = static_cast<float>(position - lowerIndex);

    const FrameSummary& lower = summaryAtFrame(lowerIndex);
    const FrameSummary& upper = summaryAtFrame(upperIndex);

    const auto lerp = [blend](float a, float b) { return a + (b - a) * blend; };
    const auto bands = static_cast<float>(std::max(params_.bandCount - 1, 1));

    return AnalysisSample{
        .rms = lerp(lower.rms, upper.rms),
        .peak = lerp(lower.peak, upper.peak),
        .bass = lerp(lower.bass, upper.bass),
        .mid = lerp(lower.mid, upper.mid),
        .high = lerp(lower.high, upper.high),
        .flux = lerp(lower.flux, upper.flux),
        .beat = lerp(lower.beat, upper.beat),
        // Not interpolated: the loudest band jumping from 3 to 90 does not
        // pass through 46, and a value that slides across the whole spectrum
        // on every transient is worse than one that snaps.
        .peakBand = static_cast<float>(lower.peakBand) / bands,
    };
}

std::int64_t AnalysisCache::beatIndexAt(Seconds t) const {
    if (beats_.empty()) {
        return -1;
    }

    // The first beat at or after t; the one we are inside is the one before.
    const auto next =
        std::upper_bound(beats_.begin(), beats_.end(), t, [](Seconds time, const Beat& beat) {
            return time < beat.time;
        });
    return std::distance(beats_.begin(), next) - 1;
}

float AnalysisCache::beatPulseAt(Seconds t) const {
    const std::int64_t index = beatIndexAt(t);
    if (index < 0) {
        return 0.0f;
    }

    const Beat& beat = beats_[static_cast<std::size_t>(index)];
    if (!(params_.pulseDecay > 0.0)) {
        return beat.strength;
    }
    return beat.strength * static_cast<float>(std::exp(-(t - beat.time) / params_.pulseDecay));
}

float AnalysisCache::beatPhaseAt(Seconds t) const {
    if (beats_.size() < 2) {
        return 0.0f;
    }

    const std::int64_t index = beatIndexAt(t);

    // Outside the detected range the grid is extrapolated on the nearest
    // known interval rather than frozen, so an intro before the first
    // detected hit still moves instead of sitting at zero.
    const auto clamped = static_cast<std::size_t>(
        std::clamp<std::int64_t>(index, 0, static_cast<std::int64_t>(beats_.size()) - 2));

    const Seconds start = beats_[clamped].time;
    const Seconds interval = beats_[clamped + 1].time - start;
    if (!(interval > 0.0)) {
        return 0.0f;
    }

    const double phase = (t - start) / interval;
    return static_cast<float>(phase - std::floor(phase));
}

Result<AnalysisCache> analyze(const AudioBuffer& audio, const AnalysisParams& params) {
    if (Result<bool> valid = params.validate(); !valid) {
        return Result<AnalysisCache>::error(valid.error());
    }
    if (audio.sampleRate != params.sampleRate) {
        return Result<AnalysisCache>::error("audio is at " + std::to_string(audio.sampleRate) +
                                            " Hz but the analysis expects " +
                                            std::to_string(params.sampleRate) +
                                            " Hz; decode with DecodeOptions{.sampleRate = ...}");
    }

    AnalysisCache cache;
    cache.params_ = params;
    cache.layout_ = makeBandLayout(params.bandCount, params.minHz, params.maxHz, params.scale);
    cache.duration_ = audio.duration();

    const std::vector<float> mono = downmixToMono(audio);
    if (mono.empty()) {
        return Result<AnalysisCache>::ok(std::move(cache));
    }

    const double hop = static_cast<double>(params.sampleRate) / params.rate;
    const int frameCount = static_cast<int>(std::floor(cache.duration_ * params.rate)) + 1;
    cache.frameCount_ = frameCount;

    const auto bandCount = static_cast<std::size_t>(params.bandCount);
    const auto totalBands = static_cast<std::size_t>(frameCount) * bandCount;

    // Phase 1, parallel: each frame's spectrum, from its own window of
    // samples and nothing else. Pure, so the work can be split any way at all
    // and the answer does not change.
    std::vector<float> raw(totalBands, 0.0f);
    cache.summaries_.assign(static_cast<std::size_t>(frameCount), FrameSummary{});

    std::vector<float> window(static_cast<std::size_t>(params.windowSize));
    fillWindow(window, params.window);
    const float windowGain = coherentGain(window);
    const float magnitudeScale =
        windowGain > 0.0f ? 2.0f / (windowGain * static_cast<float>(params.windowSize)) : 0.0f;

    const std::vector<float> tilt = tiltGains(params.tilt, cache.layout_);
    const std::vector<float> kernel = params.smoothingRadius > 0
                                          ? hammingKernel(params.smoothingRadius)
                                          : std::vector<float>{1.0f};

    const int requested = params.threadCount > 0
                              ? params.threadCount
                              : static_cast<int>(std::thread::hardware_concurrency());
    const int threadCount = std::clamp(requested, 1, std::max(frameCount, 1));

    std::vector<std::string> errors(static_cast<std::size_t>(threadCount));

    const auto runRange = [&](int worker, int firstFrame, int lastFrame) {
        Result<RealFft> fft = RealFft::create(params.windowSize);
        if (!fft) {
            errors[static_cast<std::size_t>(worker)] = fft.error();
            return;
        }

        Worker scratch{
            .fft = std::move(fft.value()),
            .samples = std::vector<float>(static_cast<std::size_t>(params.windowSize)),
            .magnitudes = std::vector<float>(static_cast<std::size_t>(params.windowSize / 2 + 1)),
            .rawBands = std::vector<float>(bandCount),
            .smoothed = std::vector<float>(bandCount),
        };
        const double binWidth = scratch.fft.binWidth(params.sampleRate);

        for (int frame = firstFrame; frame < lastFrame; ++frame) {
            const auto centre = static_cast<std::int64_t>(std::llround(frame * hop));
            readWindow(mono, centre, scratch.samples);

            FrameSummary& summary = cache.summaries_[static_cast<std::size_t>(frame)];
            double energy = 0.0;
            float peak = 0.0f;
            for (std::size_t index = 0; index < scratch.samples.size(); ++index) {
                const auto value = static_cast<double>(scratch.samples[index]);
                energy += value * value;
                peak = std::max(peak, std::abs(scratch.samples[index]));
                scratch.samples[index] *= window[index];
            }
            summary.rms =
                static_cast<float>(std::sqrt(energy / static_cast<double>(scratch.samples.size())));
            summary.peak = peak;

            scratch.fft.transform(scratch.samples, scratch.magnitudes);
            for (float& magnitude : scratch.magnitudes) {
                magnitude *= magnitudeScale;
            }

            mapBands(scratch.magnitudes, binWidth, cache.layout_, scratch.rawBands);
            for (std::size_t index = 0; index < bandCount; ++index) {
                scratch.rawBands[index] *= tilt[index];
            }

            smoothAcrossBands(scratch.rawBands, kernel, scratch.smoothed);

            const auto offset = static_cast<std::size_t>(frame) * bandCount;
            std::copy(scratch.smoothed.begin(),
                      scratch.smoothed.end(),
                      raw.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    };

    if (threadCount == 1) {
        runRange(0, 0, frameCount);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threadCount));
        const int perWorker = (frameCount + threadCount - 1) / threadCount;
        for (int worker = 0; worker < threadCount; ++worker) {
            const int firstFrame = worker * perWorker;
            const int lastFrame = std::min(firstFrame + perWorker, frameCount);
            if (firstFrame >= lastFrame) {
                break;
            }
            workers.emplace_back(runRange, worker, firstFrame, lastFrame);
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

    for (const std::string& error : errors) {
        if (!error.empty()) {
            return Result<AnalysisCache>::error(error);
        }
    }

    // Phase 2: whole-track statistics. The advantage offline analysis has
    // over any live player — the intro is scaled against the song rather than
    // against the last two seconds of it.
    constexpr double kReferencePercentile = 0.95;
    constexpr float kFloor = 1e-6f;

    std::vector<float> reference(bandCount, 1.0f);
    if (params.normalization == Normalization::PerBand) {
        std::vector<float> column(static_cast<std::size_t>(frameCount));
        for (std::size_t band = 0; band < bandCount; ++band) {
            for (int frame = 0; frame < frameCount; ++frame) {
                column[static_cast<std::size_t>(frame)] =
                    raw[static_cast<std::size_t>(frame) * bandCount + band];
            }
            reference[band] = std::max(percentile(column, kReferencePercentile), kFloor);
        }
    } else if (params.normalization == Normalization::Track) {
        std::vector<float> frameMaxima(static_cast<std::size_t>(frameCount), 0.0f);
        for (int frame = 0; frame < frameCount; ++frame) {
            const auto offset = static_cast<std::size_t>(frame) * bandCount;
            frameMaxima[static_cast<std::size_t>(frame)] =
                *std::max_element(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                  raw.begin() + static_cast<std::ptrdiff_t>(offset + bandCount));
        }
        // A silent track has no level to normalize against. Leaving the
        // reference at the floor would turn rounding noise into full-scale
        // bars, so silence stays silent.
        const float level = percentile(frameMaxima, kReferencePercentile);
        std::fill(reference.begin(), reference.end(), level > kFloor ? level : 1.0f);
    }

    // Phase 3, serial: normalize, follow the envelope, measure the flux.
    //
    // The envelope is recursive — each frame depends on the one before it —
    // so it cannot be split across threads and still give the same answer.
    // Running it here instead of inside the workers is what makes the result
    // independent of thread count by construction rather than by hoping the
    // chunk overlap was generous enough.
    cache.bands_.assign(totalBands, 0.0f);

    std::vector<EnvelopeCoefficients> coefficients(bandCount);
    for (std::size_t index = 0; index < bandCount; ++index) {
        const EnvelopeTimes& times =
            envelopeFor(params, cache.layout_.centerHz(static_cast<int>(index)));
        coefficients[index] = envelopeCoefficients(times.attack, times.decay, params.rate);
    }

    std::vector<float> previous(bandCount, 0.0f);
    std::vector<float> state(bandCount, 0.0f);
    const auto bandsPerGroup = [&](float lowHz, float highHz) {
        int count = 0;
        for (int index = 0; index < params.bandCount; ++index) {
            const float centre = cache.layout_.centerHz(index);
            if (centre >= lowHz && centre < highHz) {
                ++count;
            }
        }
        return std::max(count, 1);
    };
    const auto bassBands = static_cast<float>(bandsPerGroup(0.0f, kBassMaxHz));
    const auto midBands = static_cast<float>(bandsPerGroup(kBassMaxHz, kMidMaxHz));
    const auto highBands = static_cast<float>(bandsPerGroup(kMidMaxHz, params.maxHz + 1.0f));

    for (int frame = 0; frame < frameCount; ++frame) {
        const auto offset = static_cast<std::size_t>(frame) * bandCount;
        FrameSummary& summary = cache.summaries_[static_cast<std::size_t>(frame)];

        float flux = 0.0f;
        float loudest = -1.0f;
        float bassSum = 0.0f;
        float midSum = 0.0f;
        float highSum = 0.0f;

        for (std::size_t index = 0; index < bandCount; ++index) {
            const float normalized = raw[offset + index] / reference[index] * params.gain;

            // Flux is measured before the envelope, because smoothing is
            // exactly what removes the edges an onset detector looks for.
            flux += std::max(0.0f, normalized - previous[index]);
            previous[index] = normalized;

            state[index] = followEnvelope(state[index], normalized, coefficients[index]);
            const float value = state[index];
            cache.bands_[offset + index] = value;

            if (value > loudest) {
                loudest = value;
                summary.peakBand = static_cast<std::uint16_t>(index);
            }

            const float centre = cache.layout_.centerHz(static_cast<int>(index));
            if (centre < kBassMaxHz) {
                bassSum += value;
            } else if (centre < kMidMaxHz) {
                midSum += value;
            } else {
                highSum += value;
            }
        }

        // The first frame has nothing to differ from, and measuring it
        // against an all-zero predecessor invents an onset on the downbeat of
        // every track — including tracks that start with silence.
        summary.flux = frame == 0 ? 0.0f : flux / static_cast<float>(bandCount);
        summary.bass = bassSum / bassBands;
        summary.mid = midSum / midBands;
        summary.high = highSum / highBands;
    }

    // Phase 4: beats. Peak-picking against a centred adaptive threshold,
    // which is the one detector improvement that comes free from analysing
    // offline — the threshold at a given moment is allowed to know what
    // happens just after it.
    std::vector<float> flux(static_cast<std::size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame) {
        flux[static_cast<std::size_t>(frame)] =
            cache.summaries_[static_cast<std::size_t>(frame)].flux;
    }
    cache.beats_ = detectOnsets(flux, params.rate, params.onsets);

    // The continuous companion to the discrete beats: flux scaled against
    // this track's own distribution, so 1.0 means "about as much as ever
    // changes at once in this song".
    std::vector<float> fluxCopy = flux;
    const float fluxReference = percentile(fluxCopy, kReferencePercentile);
    if (fluxReference > kFloor) {
        for (int frame = 0; frame < frameCount; ++frame) {
            FrameSummary& summary = cache.summaries_[static_cast<std::size_t>(frame)];
            summary.beat = std::min(summary.flux / fluxReference, 1.0f);
        }
    }

    return Result<AnalysisCache>::ok(std::move(cache));
}

Result<AnalysisCache> analyzeFile(const std::filesystem::path& path, const AnalysisParams& params) {
    if (Result<bool> valid = params.validate(); !valid) {
        return Result<AnalysisCache>::error(valid.error());
    }

    Result<AudioBuffer> audio = decodeAudioFile(path,
                                                DecodeOptions{
                                                    .sampleRate = params.sampleRate,
                                                    .channels = 1,
                                                });
    if (!audio) {
        return Result<AnalysisCache>::error(audio.error());
    }
    return analyze(audio.value(), params);
}

}  // namespace spectra::audio
