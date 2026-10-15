// The Stage 3 exit demonstration from docs/15-BUILD-ORDER.md:
//
//   "load a track, play it, and query any signal at any t with bit-identical
//    results on repeat."
//
// Run it with a path to any audio file, or with no arguments to have it
// generate one. It analyses the track, plays it while drawing the spectrum
// from the cache, and then re-queries a few thousand times in a scrambled
// order and compares against the first pass.
//
// Not a test: it needs a sound card and its value is in being watched. The
// claims it makes are checked headlessly in audio/tests/.

#include <spectra/audio/cache_file.hpp>
#include <spectra/audio/decoder.hpp>
#include <spectra/audio/playback.hpp>
#include <spectra/audio/signal_provider.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace spectra;
using namespace spectra::audio;

namespace {

constexpr int kBars = 48;

/// A kick on every beat with a chord over it, for when there is no track to
/// hand. Enough structure that the bars move and the beat detector has
/// something to find.
std::filesystem::path writeDemoTrack(const std::filesystem::path& path) {
    constexpr int kRate = 44100;
    constexpr double kSeconds = 12.0;
    constexpr double kBpm = 124.0;

    std::vector<float> samples(static_cast<std::size_t>(kSeconds * kRate), 0.0f);
    const double beat = 60.0 / kBpm;

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const double t = static_cast<double>(index) / kRate;
        const double sinceBeat = std::fmod(t, beat);

        // Kick: a falling sine with a fast decay.
        const double thump = std::exp(-sinceBeat * 18.0);
        const double sweep = 110.0 * std::exp(-sinceBeat * 9.0) + 42.0;
        double value = 0.7 * thump * std::sin(2.0 * std::numbers::pi * sweep * t);

        // Hat on the off-beat.
        const double sinceHat = std::fmod(t + beat / 2.0, beat);
        value += 0.12 * std::exp(-sinceHat * 90.0) * std::sin(2.0 * std::numbers::pi * 9000.0 * t);

        // A chord underneath, so the mid bands are not empty.
        for (const double partial : {220.0, 277.18, 329.63}) {
            value += 0.07 * std::sin(2.0 * std::numbers::pi * partial * t);
        }

        samples[index] = static_cast<float>(std::clamp(value, -1.0, 1.0));
    }

    // A 16-bit WAV, written by hand. One fewer thing to depend on.
    std::vector<unsigned char> bytes;
    const auto put32 = [&bytes](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
        }
    };
    const auto put16 = [&bytes](std::uint16_t value) {
        bytes.push_back(static_cast<unsigned char>(value & 0xFFu));
        bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
    };
    const auto putTag = [&bytes](const char* tag) {
        for (int index = 0; index < 4; ++index) {
            bytes.push_back(static_cast<unsigned char>(tag[index]));
        }
    };

    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    putTag("RIFF");
    put32(36 + dataBytes);
    putTag("WAVE");
    putTag("fmt ");
    put32(16);
    put16(1);
    put16(1);
    put32(kRate);
    put32(kRate * 2);
    put16(2);
    put16(16);
    putTag("data");
    put32(dataBytes);
    for (const float sample : samples) {
        put16(
            static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(sample * 32767.0f))));
    }

    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file != nullptr) {
        std::fwrite(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
    }
    return path;
}

void drawSpectrum(const AnalysisCache& cache, const AnalysisSignalProvider& signals, Seconds t) {
    std::string line;
    line.reserve(kBars * 4);

    const int bandsPerBar = std::max(cache.bandCount() / kBars, 1);
    for (int bar = 0; bar < kBars; ++bar) {
        float value = 0.0f;
        for (int offset = 0; offset < bandsPerBar; ++offset) {
            value = std::max(value, cache.band(bar * bandsPerBar + offset, t));
        }

        static constexpr const char* kBlocks[] = {
            " ", "\u2581", "\u2582", "\u2583", "\u2584", "\u2585", "\u2586", "\u2587"};
        const auto level = static_cast<int>(std::clamp(value, 0.0f, 0.999f) * 8.0f);
        line += kBlocks[level];
    }

    const float pulse = signals.sample(SignalRef{.name = "beatPulse"}, t);
    const float phase = signals.sample(SignalRef{.name = "beatPhase"}, t);
    const auto beat = static_cast<long long>(signals.beatIndex(t));

    std::printf("\r%6.2fs |%s| beat %-4lld phase %.2f pulse %-5.2f",
                t,
                line.c_str(),
                beat,
                static_cast<double>(phase),
                static_cast<double>(pulse));
    std::fflush(stdout);
}

/// Every signal at every time in the list, in the order given.
std::vector<float> sampleAll(const AnalysisSignalProvider& signals,
                             const std::vector<Seconds>& times) {
    static const std::vector<std::string> kNames{"rms",
                                                 "peak",
                                                 "bass",
                                                 "mid",
                                                 "high",
                                                 "beat",
                                                 "beatPulse",
                                                 "beatPhase",
                                                 "beatIndex",
                                                 "peakBand",
                                                 "trackPos",
                                                 "band",
                                                 "bandRange"};

    std::vector<float> values(times.size() * kNames.size());
    for (std::size_t index = 0; index < times.size(); ++index) {
        for (std::size_t name = 0; name < kNames.size(); ++name) {
            const SignalRef ref{.name = kNames[name], .argA = 32.0f, .argB = 4000.0f};
            values[index * kNames.size() + name] = signals.sample(ref, times[index]);
        }
    }
    return values;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path path;
    if (argc > 1) {
        path = argv[1];
    } else {
        path = std::filesystem::temp_directory_path() / "spectra-demo-track.wav";
        std::printf("No track given, writing one to %s\n", path.string().c_str());
        writeDemoTrack(path);
    }

    const std::filesystem::path cacheDirectory =
        std::filesystem::temp_directory_path() / "spectra-analysis";

    const auto started = std::chrono::steady_clock::now();
    Result<AnalysisCache> analysed = analyzeFileCached(path, cacheDirectory);
    const auto finished = std::chrono::steady_clock::now();

    if (!analysed) {
        std::fprintf(stderr, "%s\n", analysed.error().c_str());
        return 1;
    }

    auto cache = std::make_shared<const AnalysisCache>(std::move(analysed.value()));
    const AnalysisSignalProvider signals(cache);

    // Onsets per minute, not BPM. The detector finds every hi-hat as well as
    // every kick, and turning that into a tempo needs the Phase 2 grid fit.
    double perMinute = 0.0;
    if (cache->duration() > 0.0) {
        perMinute = 60.0 * static_cast<double>(cache->beats().size()) / cache->duration();
    }

    std::printf("%s\n", path.filename().string().c_str());
    std::printf("  %.1f s, %d frames at %.0f Hz, %d bands\n",
                cache->duration(),
                cache->frameCount(),
                cache->rate(),
                cache->bandCount());
    std::printf("  %zu onsets, %.0f per minute\n", cache->beats().size(), perMinute);
    std::printf("  ready in %.0f ms\n\n",
                std::chrono::duration<double, std::milli>(finished - started).count());

    // ---- The exit criterion, part one: play it. ----------------------------
    Result<AudioBuffer> decoded = decodeAudioFile(path);
    if (!decoded) {
        std::fprintf(stderr, "%s\n", decoded.error().c_str());
        return 1;
    }

    Result<Player> player =
        Player::create(std::make_shared<AudioBuffer>(std::move(decoded.value())));
    if (!player) {
        std::printf("No audio output (%s) — showing the analysis on a virtual clock instead.\n\n",
                    player.error().c_str());

        for (Seconds t = 0.0; t < cache->duration(); t += 1.0 / 30.0) {
            drawSpectrum(*cache, signals, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    } else {
        std::printf("latency %.0f ms\n", player.value().offsets().deviceLatency * 1000.0);
        player.value().play();

        while (player.value().playing()) {
            drawSpectrum(*cache, signals, player.value().visualPosition());
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    std::printf("\n\n");

    // ---- The exit criterion, part two: query it, twice, out of order. ------
    std::vector<Seconds> times;
    for (int step = 0; step < 4000; ++step) {
        times.push_back(cache->duration() * step / 4000.0);
    }

    const std::vector<float> inOrder = sampleAll(signals, times);

    std::vector<Seconds> scrambled = times;
    std::mt19937 generator(20261015);
    std::shuffle(scrambled.begin(), scrambled.end(), generator);
    std::vector<float> outOfOrder = sampleAll(signals, scrambled);

    // Put the scrambled results back where they belong before comparing.
    std::vector<float> reordered(inOrder.size());
    const std::size_t stride = inOrder.size() / times.size();
    for (std::size_t index = 0; index < scrambled.size(); ++index) {
        const auto original =
            static_cast<std::size_t>(std::llround(scrambled[index] / cache->duration() * 4000.0));
        std::copy_n(outOfOrder.begin() + static_cast<std::ptrdiff_t>(index * stride),
                    stride,
                    reordered.begin() + static_cast<std::ptrdiff_t>(original * stride));
    }

    const bool identical = inOrder == reordered;
    std::printf("%zu signal queries, forwards and scrambled: %s\n",
                inOrder.size(),
                identical ? "bit-identical" : "DIFFERENT");

    return identical ? 0 : 1;
}
