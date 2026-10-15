#include <spectra/audio/cache_file.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <sstream>

namespace spectra::audio {
namespace {

// The format is raw little-endian records with no byte-swapping anywhere.
// Both platforms we target are little-endian, and a cache is a local
// performance artefact rather than something anyone transfers between
// machines — if that ever stops being true, the version number is how we
// find out.
static_assert(std::endian::native == std::endian::little,
              "the analysis cache format assumes little-endian");

constexpr std::array<char, 4> kMagic{'S', 'B', 'A', 'C'};

/// Bump on any layout change. Old files then fail to load and are recomputed,
/// which is the entire migration strategy a cache needs.
constexpr std::uint32_t kVersion = 1;

template<typename T>
void write(std::ostream& out, const T& value) {
    static_assert(std::is_arithmetic_v<T>);
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template<typename T>
bool read(std::istream& in, T& value) {
    static_assert(std::is_arithmetic_v<T>);
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return in.good();
}

std::string toHex(std::uint64_t value) {
    std::ostringstream text;
    text << std::hex << value;
    return text.str();
}

}  // namespace

Result<std::uint64_t> hashAudioFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::uint64_t>::error("cannot read " + path.string());
    }

    // FNV-1a. Not cryptographic, and does not need to be: the worst a
    // collision can do is reuse an analysis, and the cost of that is one
    // wrong-looking preview until somebody re-renders.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    std::array<char, 1 << 16> buffer{};
    std::uint64_t length = 0;

    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        length += got;
        for (std::size_t index = 0; index < got; ++index) {
            hash ^= static_cast<unsigned char>(buffer[index]);
            hash *= 0x100000001b3ULL;
        }
    }

    if (in.bad()) {
        return Result<std::uint64_t>::error("error reading " + path.string());
    }

    // Length as well, so that a truncation which happens to leave the hash
    // intact still invalidates.
    hash ^= length;
    hash *= 0x100000001b3ULL;
    return Result<std::uint64_t>::ok(hash);
}

std::filesystem::path analysisCachePath(const std::filesystem::path& directory,
                                        std::uint64_t audioHash,
                                        std::uint64_t paramsHash) {
    return directory / (toHex(audioHash) + "-" + toHex(paramsHash) + ".sbac");
}

Result<bool> writeAnalysisCache(const std::filesystem::path& path,
                                const AnalysisCache& cache,
                                std::uint64_t audioHash) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);

    // Written beside the target and renamed, so that a crash or a full disk
    // leaves no half-written cache for the next run to trust. The name check
    // on read would not catch it: the name would be right and the contents
    // would not.
    const std::filesystem::path temporary = path.string() + ".partial";

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<bool>::error("cannot write " + temporary.string());
        }

        out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        write(out, kVersion);
        write(out, audioHash);
        write(out, cache.params().hash());
        write(out, static_cast<std::uint32_t>(cache.frameCount()));
        write(out, static_cast<std::uint32_t>(cache.bandCount()));
        write(out, cache.duration());
        write(out, static_cast<std::uint32_t>(cache.beats().size()));

        const std::vector<float>& edges = cache.layout().edges;
        write(out, static_cast<std::uint32_t>(edges.size()));
        out.write(reinterpret_cast<const char*>(edges.data()),
                  static_cast<std::streamsize>(edges.size() * sizeof(float)));

        // Full float32, not the float16 docs/04-AUDIO-ENGINE.md estimated
        // with. Halving the file would make a loaded cache differ from a
        // fresh analysis, and then whether preview and export agree would
        // depend on whether the cache happened to be warm. 49 MB for a
        // four-minute track is the price of that not being a question.
        out.write(reinterpret_cast<const char*>(cache.bands_.data()),
                  static_cast<std::streamsize>(cache.bands_.size() * sizeof(float)));

        // Field by field rather than as a struct: padding bytes are never
        // initialised, and writing them makes two identical analyses produce
        // different files.
        for (const FrameSummary& summary : cache.summaries_) {
            write(out, summary.rms);
            write(out, summary.peak);
            write(out, summary.bass);
            write(out, summary.mid);
            write(out, summary.high);
            write(out, summary.flux);
            write(out, summary.beat);
            write(out, summary.peakBand);
        }

        for (const Beat& beat : cache.beats_) {
            write(out, beat.time);
            write(out, beat.strength);
        }

        out.flush();
        if (!out) {
            return Result<bool>::error("error writing " + temporary.string());
        }
    }

    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::filesystem::remove(temporary, ignored);
        return Result<bool>::error("cannot replace " + path.string() + ": " +
                                   renameError.message());
    }

    return Result<bool>::ok(true);
}

Result<AnalysisCache> readAnalysisCache(const std::filesystem::path& path,
                                        const AnalysisParams& params,
                                        std::uint64_t audioHash) {
    const auto stale = [](const std::string& why) { return Result<AnalysisCache>::error(why); };

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return stale("no cache at " + path.string());
    }

    std::array<char, 4> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in.good() || magic != kMagic) {
        return stale("not an analysis cache: " + path.string());
    }

    std::uint32_t version = 0;
    std::uint64_t storedAudioHash = 0;
    std::uint64_t storedParamsHash = 0;
    std::uint32_t frameCount = 0;
    std::uint32_t bandCount = 0;
    Seconds duration = 0.0;
    std::uint32_t beatCount = 0;
    std::uint32_t edgeCount = 0;

    if (!read(in, version) || !read(in, storedAudioHash) || !read(in, storedParamsHash) ||
        !read(in, frameCount) || !read(in, bandCount) || !read(in, duration) ||
        !read(in, beatCount) || !read(in, edgeCount)) {
        return stale("truncated cache header in " + path.string());
    }

    if (version != kVersion) {
        return stale("cache format " + std::to_string(version) + ", expected " +
                     std::to_string(kVersion));
    }
    if (storedAudioHash != audioHash) {
        return stale("cache is for different audio");
    }
    if (storedParamsHash != params.hash()) {
        return stale("cache is for different analysis parameters");
    }
    if (bandCount != static_cast<std::uint32_t>(params.bandCount) ||
        edgeCount != static_cast<std::uint32_t>(params.bandCount) + 1) {
        return stale("cache band count disagrees with its own parameters");
    }

    AnalysisCache cache;
    cache.params_ = params;
    cache.frameCount_ = static_cast<int>(frameCount);
    cache.duration_ = duration;

    cache.layout_.edges.resize(edgeCount);
    in.read(reinterpret_cast<char*>(cache.layout_.edges.data()),
            static_cast<std::streamsize>(edgeCount * sizeof(float)));

    cache.bands_.resize(static_cast<std::size_t>(frameCount) * bandCount);
    in.read(reinterpret_cast<char*>(cache.bands_.data()),
            static_cast<std::streamsize>(cache.bands_.size() * sizeof(float)));

    cache.summaries_.resize(frameCount);
    for (FrameSummary& summary : cache.summaries_) {
        if (!read(in, summary.rms) || !read(in, summary.peak) || !read(in, summary.bass) ||
            !read(in, summary.mid) || !read(in, summary.high) || !read(in, summary.flux) ||
            !read(in, summary.beat) || !read(in, summary.peakBand)) {
            return stale("truncated cache body in " + path.string());
        }
    }

    cache.beats_.resize(beatCount);
    for (Beat& beat : cache.beats_) {
        if (!read(in, beat.time) || !read(in, beat.strength)) {
            return stale("truncated beat list in " + path.string());
        }
    }

    if (!in.good()) {
        return stale("truncated cache in " + path.string());
    }

    return Result<AnalysisCache>::ok(std::move(cache));
}

Result<AnalysisCache> analyzeFileCached(const std::filesystem::path& audioPath,
                                        const std::filesystem::path& cacheDirectory,
                                        const AnalysisParams& params) {
    Result<std::uint64_t> audioHash = hashAudioFile(audioPath);
    if (!audioHash) {
        return Result<AnalysisCache>::error(audioHash.error());
    }

    const std::filesystem::path path =
        analysisCachePath(cacheDirectory, audioHash.value(), params.hash());
    if (Result<AnalysisCache> cached = readAnalysisCache(path, params, audioHash.value())) {
        return cached;
    }

    Result<AnalysisCache> fresh = analyzeFile(audioPath, params);
    if (!fresh) {
        return fresh;
    }

    // A cache that will not write is a performance problem, not a failure.
    // Read-only media, a full disk, a project opened from an archive: the
    // analysis is still correct and the user still wants it.
    static_cast<void>(writeAnalysisCache(path, fresh.value(), audioHash.value()));

    return fresh;
}

}  // namespace spectra::audio
