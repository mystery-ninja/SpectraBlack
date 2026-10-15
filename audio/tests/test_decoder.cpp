#include "wav_fixture.hpp"

#include <spectra/audio/decoder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace spectra;
using namespace spectra::audio;
using namespace spectra::audio::test;

namespace {

float peakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (const float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

/// Root-mean-square, as a crude "did the audio survive" check for the lossy
/// formats where sample-by-sample comparison is meaningless.
double rmsOf(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float sample : samples) {
        const auto value = static_cast<double>(sample);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

#ifdef SPECTRA_FFMPEG_EXECUTABLE
/// Transcodes through the ffmpeg binary rather than linking its encoders into
/// the test, which would put libav* in a second translation unit for the sake
/// of a fixture. Returns false when the build has no ffmpeg to call.
bool transcode(const std::filesystem::path& from, const std::filesystem::path& to) {
    const std::string command = std::string(SPECTRA_FFMPEG_EXECUTABLE) +
                                " -hide_banner -loglevel error -y -i \"" + from.string() + "\" \"" +
                                to.string() + "\" 2>/dev/null";
    return std::system(command.c_str()) == 0 && std::filesystem::exists(to);
}
#endif

}  // namespace

TEST_CASE("float WAV round-trips bit-exactly") {
    const TempDir dir;
    const auto path = dir.file("sine.wav");
    const AudioBuffer original = makeSine(440.0, 0.25, 48000, 2);
    writeWav(path, original, WavSampleType::Float32);

    const Result<AudioBuffer> decoded = decodeAudioFile(path);

    REQUIRE_MESSAGE(decoded, decoded.error());
    CHECK(decoded.value().sampleRate == 48000);
    CHECK(decoded.value().channels == 2);
    CHECK(decoded.value().samples == original.samples);
}

TEST_CASE("16-bit WAV round-trips within one quantization step") {
    const TempDir dir;
    const auto path = dir.file("sine16.wav");
    const AudioBuffer original = makeSine(440.0, 0.1, 44100, 1);
    writeWav(path, original, WavSampleType::Int16);

    const Result<AudioBuffer> decoded = decodeAudioFile(path);

    REQUIRE_MESSAGE(decoded, decoded.error());
    REQUIRE(decoded.value().samples.size() == original.samples.size());

    // Half a step from rounding on the way in, plus up to another step from
    // the asymmetry everyone lives with: full scale encodes as 32767 but
    // decodes as a division by 32768.
    constexpr float kTolerance = 1.5f / 32768.0f;
    float worst = 0.0f;
    for (std::size_t index = 0; index < original.samples.size(); ++index) {
        worst = std::max(worst, std::abs(decoded.value().samples[index] - original.samples[index]));
    }
    CHECK(worst <= kTolerance);
}

TEST_CASE("probe describes the file without decoding it") {
    const TempDir dir;
    const auto path = dir.file("probe.wav");
    writeWav(path, makeSine(1000.0, 1.5, 44100, 2), WavSampleType::Int16);

    const Result<AudioFileInfo> info = probeAudioFile(path);

    REQUIRE_MESSAGE(info, info.error());
    CHECK(info.value().sampleRate == 44100);
    CHECK(info.value().channels == 2);
    CHECK(info.value().duration == doctest::Approx(1.5).epsilon(0.01));
    CHECK(info.value().codec == "pcm_s16le");
    CHECK(info.value().container == "wav");
}

TEST_CASE("decoding resamples on request") {
    const TempDir dir;
    const auto path = dir.file("48k.wav");
    writeWav(path, makeSine(440.0, 1.0, 48000, 2), WavSampleType::Float32);

    const Result<AudioBuffer> decoded = decodeAudioFile(path, DecodeOptions{.sampleRate = 44100});

    REQUIRE_MESSAGE(decoded, decoded.error());
    CHECK(decoded.value().sampleRate == 44100);

    // The resampler's filter costs a few milliseconds at each end; what must
    // not change is how long the track is in seconds.
    CHECK(decoded.value().duration() == doctest::Approx(1.0).epsilon(0.01));
    CHECK(peakOf(decoded.value().samples) == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE("decoding downmixes on request") {
    const TempDir dir;
    const auto path = dir.file("stereo.wav");
    writeWav(path, makeSine(440.0, 0.25, 48000, 2), WavSampleType::Float32);

    const Result<AudioBuffer> decoded = decodeAudioFile(path, DecodeOptions{.channels = 1});

    REQUIRE_MESSAGE(decoded, decoded.error());
    CHECK(decoded.value().channels == 1);
    CHECK(decoded.value().duration() == doctest::Approx(0.25).epsilon(0.01));
    CHECK(peakOf(decoded.value().samples) == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("a missing file is an error, not a crash") {
    const Result<AudioBuffer> decoded = decodeAudioFile("/nonexistent/spectra/no-such-track.wav");

    REQUIRE_FALSE(decoded);
    CHECK_FALSE(decoded.error().empty());
}

TEST_CASE("a file that is not audio is an error") {
    const TempDir dir;
    const auto path = dir.file("notes.txt");
    {
        std::ofstream out(path);
        out << "This is not a sound.";
    }

    CHECK_FALSE(decodeAudioFile(path));
    CHECK_FALSE(probeAudioFile(path));
}

TEST_CASE("decoding is reproducible") {
    // The weaker half of the Stage 3 determinism requirement, and the half
    // that is cheap to assert here. Analysis determinism is tested where
    // analysis lives.
    const TempDir dir;
    const auto path = dir.file("twice.wav");
    writeWav(path, makeSine(220.0, 0.2, 44100, 2), WavSampleType::Int16);

    const Result<AudioBuffer> first =
        decodeAudioFile(path, DecodeOptions{.sampleRate = 48000, .channels = 1});
    const Result<AudioBuffer> second =
        decodeAudioFile(path, DecodeOptions{.sampleRate = 48000, .channels = 1});

    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.value().samples == second.value().samples);
}

#ifdef SPECTRA_FFMPEG_EXECUTABLE
TEST_CASE("the compressed formats decode") {
    const TempDir dir;
    const auto source = dir.file("source.wav");
    const AudioBuffer original = makeSine(440.0, 1.0, 44100, 2);
    writeWav(source, original, WavSampleType::Int16);
    const double sourceRms = rmsOf(original.samples);

    // Not every build of ffmpeg has every encoder — mp3 needs LAME, which is
    // a separate package on some distributions. A format the machine cannot
    // produce is skipped rather than failed.
    for (const char* extension : {"flac", "ogg", "mp3", "m4a"}) {
        CAPTURE(extension);
        const auto encoded = dir.file(std::string("track.") + extension);
        if (!transcode(source, encoded)) {
            MESSAGE("no encoder for " << extension << ", skipping");
            continue;
        }

        const Result<AudioBuffer> decoded = decodeAudioFile(encoded);
        REQUIRE_MESSAGE(decoded, decoded.error());
        CHECK(decoded.value().channels == 2);
        CHECK(decoded.value().sampleRate == 44100);

        // Lossy codecs pad the start and change every sample, so the claim is
        // only that a second of 440 Hz at this level came back as a second of
        // audio at roughly this level.
        CHECK(decoded.value().duration() == doctest::Approx(1.0).epsilon(0.1));
        CHECK(rmsOf(decoded.value().samples) == doctest::Approx(sourceRms).epsilon(0.1));
    }
}
#endif
