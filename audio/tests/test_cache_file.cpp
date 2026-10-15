#include "wav_fixture.hpp"

#include <spectra/audio/cache_file.hpp>

#include <doctest/doctest.h>

#include <fstream>

using namespace spectra;
using namespace spectra::audio;
using namespace spectra::audio::test;

namespace {

AnalysisParams fastParams() {
    return AnalysisParams{
        .sampleRate = 44100,
        .rate = 100.0,
        .windowSize = 1024,
        .bandCount = 32,
    };
}

void writeTone(const std::filesystem::path& path, double frequency = 440.0, double seconds = 1.0) {
    writeWav(path, makeSine(frequency, seconds, 44100, 1), WavSampleType::Float32);
}

}  // namespace

TEST_CASE("hashing is by content, not by path or timestamp") {
    const TempDir dir;
    const auto first = dir.file("a.wav");
    const auto second = dir.file("b.wav");
    const auto different = dir.file("c.wav");

    writeTone(first);
    writeTone(second);
    writeTone(different, 880.0);

    const Result<std::uint64_t> a = hashAudioFile(first);
    const Result<std::uint64_t> b = hashAudioFile(second);
    const Result<std::uint64_t> c = hashAudioFile(different);

    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);

    // Copying a file, or restoring it from a backup, must not throw away a
    // cache that is still correct.
    CHECK(a.value() == b.value());
    CHECK(a.value() != c.value());
}

TEST_CASE("truncation invalidates even if the prefix is identical") {
    const TempDir dir;
    const auto full = dir.file("full.wav");
    const auto cut = dir.file("cut.wav");
    writeTone(full);

    const auto size = std::filesystem::file_size(full);
    std::filesystem::copy_file(full, cut);
    std::filesystem::resize_file(cut, size / 2);

    CHECK(hashAudioFile(full).value() != hashAudioFile(cut).value());
}

TEST_CASE("hashing a missing file is an error") {
    CHECK_FALSE(hashAudioFile("/nonexistent/spectra/nothing.wav"));
}

TEST_CASE("the cache path names both hashes") {
    const std::filesystem::path path = analysisCachePath("/tmp/cache", 0xabc, 0xdef);

    CHECK(path.parent_path() == "/tmp/cache");
    CHECK(path.filename().string().find("abc") != std::string::npos);
    CHECK(path.filename().string().find("def") != std::string::npos);
    CHECK(path.extension() == ".sbac");
}

TEST_CASE("a cache round-trips exactly") {
    // Exactly, not approximately. A loaded cache that differs from a fresh
    // analysis would make preview and export agree or disagree depending on
    // whether the cache happened to be warm.
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio, 1000.0, 2.0);

    const AnalysisParams params = fastParams();
    const Result<AnalysisCache> original = analyzeFile(audio, params);
    REQUIRE_MESSAGE(original, original.error());

    const Result<std::uint64_t> hash = hashAudioFile(audio);
    REQUIRE(hash);

    const auto path = dir.file("cache.sbac");
    const Result<bool> written = writeAnalysisCache(path, original.value(), hash.value());
    REQUIRE_MESSAGE(written, written.error());

    const Result<AnalysisCache> loaded = readAnalysisCache(path, params, hash.value());
    REQUIRE_MESSAGE(loaded, loaded.error());

    REQUIRE(loaded.value().frameCount() == original.value().frameCount());
    REQUIRE(loaded.value().bandCount() == original.value().bandCount());
    CHECK(loaded.value().duration() == original.value().duration());
    CHECK(loaded.value().layout().edges == original.value().layout().edges);

    for (int frame = 0; frame < original.value().frameCount(); ++frame) {
        const std::span<const float> a = original.value().bandsAtFrame(frame);
        const std::span<const float> b = loaded.value().bandsAtFrame(frame);
        REQUIRE(std::equal(a.begin(), a.end(), b.begin()));

        const FrameSummary& expected = original.value().summaryAtFrame(frame);
        const FrameSummary& actual = loaded.value().summaryAtFrame(frame);
        REQUIRE(actual.rms == expected.rms);
        REQUIRE(actual.flux == expected.flux);
        REQUIRE(actual.beat == expected.beat);
        REQUIRE(actual.peakBand == expected.peakBand);
    }

    REQUIRE(loaded.value().beats().size() == original.value().beats().size());
    for (std::size_t index = 0; index < original.value().beats().size(); ++index) {
        CHECK(loaded.value().beats()[index].time == original.value().beats()[index].time);
        CHECK(loaded.value().beats()[index].strength == original.value().beats()[index].strength);
    }
}

TEST_CASE("a cache for different audio or different parameters is refused") {
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio);

    const AnalysisParams params = fastParams();
    const Result<AnalysisCache> cache = analyzeFile(audio, params);
    REQUIRE_MESSAGE(cache, cache.error());

    const auto path = dir.file("cache.sbac");
    REQUIRE(writeAnalysisCache(path, cache.value(), 1234));

    CHECK(readAnalysisCache(path, params, 1234));
    CHECK_FALSE(readAnalysisCache(path, params, 9999));

    AnalysisParams other = params;
    other.bandCount = 64;
    CHECK_FALSE(readAnalysisCache(path, other, 1234));

    // Thread count cannot change the result, so it must not invalidate.
    AnalysisParams threaded = params;
    threaded.threadCount = 4;
    CHECK(readAnalysisCache(path, threaded, 1234));
}

TEST_CASE("a damaged cache is refused rather than repaired") {
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio);
    const AnalysisParams params = fastParams();
    const Result<AnalysisCache> cache = analyzeFile(audio, params);
    REQUIRE(cache);

    SUBCASE("missing") {
        CHECK_FALSE(readAnalysisCache(dir.file("absent.sbac"), params, 1));
    }

    SUBCASE("not a cache at all") {
        const auto path = dir.file("junk.sbac");
        std::ofstream(path, std::ios::binary) << "this is not an analysis cache, it is a sandwich";
        CHECK_FALSE(readAnalysisCache(path, params, 1));
    }

    SUBCASE("truncated") {
        const auto path = dir.file("short.sbac");
        REQUIRE(writeAnalysisCache(path, cache.value(), 1));
        std::filesystem::resize_file(path, std::filesystem::file_size(path) / 3);
        CHECK_FALSE(readAnalysisCache(path, params, 1));
    }

    SUBCASE("empty") {
        const auto path = dir.file("empty.sbac");
        std::ofstream(path, std::ios::binary);
        CHECK_FALSE(readAnalysisCache(path, params, 1));
    }
}

TEST_CASE("no half-written cache is left behind") {
    // Written beside the target and renamed into place. The hashes in the
    // name cannot catch a torn write, because the name would be right and
    // only the contents wrong.
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio);
    const Result<AnalysisCache> cache = analyzeFile(audio, fastParams());
    REQUIRE(cache);

    const auto path = dir.file("sub/dir/cache.sbac");
    REQUIRE(writeAnalysisCache(path, cache.value(), 7));

    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path.string() + ".partial"));
}

TEST_CASE("analyzeFileCached computes once and reads thereafter") {
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio, 1000.0, 2.0);
    const auto cacheDir = dir.file("cache");
    const AnalysisParams params = fastParams();

    const Result<AnalysisCache> first = analyzeFileCached(audio, cacheDir, params);
    REQUIRE_MESSAGE(first, first.error());
    CHECK(std::filesystem::exists(
        analysisCachePath(cacheDir, hashAudioFile(audio).value(), params.hash())));

    const Result<AnalysisCache> second = analyzeFileCached(audio, cacheDir, params);
    REQUIRE_MESSAGE(second, second.error());

    REQUIRE(second.value().frameCount() == first.value().frameCount());
    for (int frame = 0; frame < first.value().frameCount(); ++frame) {
        const std::span<const float> a = first.value().bandsAtFrame(frame);
        const std::span<const float> b = second.value().bandsAtFrame(frame);
        REQUIRE(std::equal(a.begin(), a.end(), b.begin()));
    }
}

TEST_CASE("a cache directory that cannot be written costs speed, not function") {
    const TempDir dir;
    const auto audio = dir.file("tone.wav");
    writeTone(audio);

    // A project opened from read-only media still wants its analysis.
    const Result<AnalysisCache> cache =
        analyzeFileCached(audio, "/proc/spectra-cannot-write", fastParams());

    REQUIRE_MESSAGE(cache, cache.error());
    CHECK(cache.value().frameCount() > 0);
}

TEST_CASE("analyzeFileCached reports a missing track") {
    const TempDir dir;

    CHECK_FALSE(analyzeFileCached(dir.file("absent.wav"), dir.file("cache"), fastParams()));
}
