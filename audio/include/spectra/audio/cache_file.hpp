#pragma once

#include <spectra/audio/analysis.hpp>
#include <spectra/core/result.hpp>

#include <cstdint>
#include <filesystem>

namespace spectra::audio {

/// Hash of an audio file's entire contents, plus its length.
///
/// Contents rather than path and modification time. Modification time is
/// wrong in both directions: it changes when a file is copied or restored
/// from a backup, throwing away a cache that is still valid, and it does not
/// change when a file is replaced by one of the same length by a tool that
/// preserves timestamps. Reading 50 MB to decide takes tens of milliseconds
/// and decoding it would take a hundred times that.
[[nodiscard]] Result<std::uint64_t> hashAudioFile(const std::filesystem::path& path);

/// Where the cache for one track analysed one way lives.
///
/// Both hashes are in the name, so a stale file is never read — it simply is
/// not the file we look for. Changing any analysis parameter changes the name
/// and the old cache is left alone, which means flipping a setting back and
/// forth is free rather than a recompute each way.
[[nodiscard]] std::filesystem::path analysisCachePath(const std::filesystem::path& directory,
                                                      std::uint64_t audioHash,
                                                      std::uint64_t paramsHash);

[[nodiscard]] Result<bool> writeAnalysisCache(const std::filesystem::path& path,
                                              const AnalysisCache& cache,
                                              std::uint64_t audioHash);

/// Reads a cache written for exactly these parameters and this audio.
///
/// A mismatch of either hash, a bad magic number, an older format version or
/// a truncated file are all the same answer — an error, meaning "analyse it
/// again". A cache is never worth a repair attempt.
[[nodiscard]] Result<AnalysisCache> readAnalysisCache(const std::filesystem::path& path,
                                                      const AnalysisParams& params,
                                                      std::uint64_t audioHash);

/// Analyse a track, reading the cache if one is there and writing it if not.
///
/// The usual entry point once there is a project to keep caches next to. A
/// cache that cannot be written is not an error: the analysis succeeded, and
/// a read-only directory should cost performance rather than function.
[[nodiscard]] Result<AnalysisCache> analyzeFileCached(const std::filesystem::path& audioPath,
                                                      const std::filesystem::path& cacheDirectory,
                                                      const AnalysisParams& params = {});

}  // namespace spectra::audio
