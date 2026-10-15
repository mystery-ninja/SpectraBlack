#pragma once

#include <spectra/audio/buffer.hpp>
#include <spectra/core/result.hpp>
#include <spectra/core/types.hpp>

#include <filesystem>
#include <string>

namespace spectra::audio {

/// What the container claims, read without decoding any audio.
///
/// Worth having separately because the editor wants to show a track's length
/// and format the instant it is dropped in, while the full decode of a
/// 10-minute FLAC takes long enough to be worth a progress indicator.
struct AudioFileInfo {
    Seconds duration{0.0};
    int sampleRate{0};
    int channels{0};
    std::string codec;      ///< e.g. "flac", "mp3"
    std::string container;  ///< e.g. "wav", "ogg"
};

struct DecodeOptions {
    /// Target rate in Hz, or 0 to keep the file's own.
    ///
    /// Analysis asks for a fixed rate so that a window length in samples means
    /// a fixed length in milliseconds regardless of the source, which is what
    /// makes the analysis parameters in docs/04-AUDIO-ENGINE.md mean anything.
    int sampleRate{0};

    /// Target channel count, or 0 to keep the file's own.
    ///
    /// Asking for 1 produces exactly what `downmixToMono` would — a plain
    /// channel average — so analysis gets the same signal either way.
    int channels{0};
};

/// Decode an entire audio file to interleaved float PCM.
///
/// Handles anything FFmpeg does — MP3, FLAC, WAV, OGG and the rest — including
/// files whose audio is a stream inside a video container.
///
/// Whole-file rather than streaming because analysis is offline and wants the
/// whole track anyway (docs/04-AUDIO-ENGINE.md), and because an hour of stereo
/// 44.1 kHz float is 600 MB, which is large but not a reason for complexity we
/// would otherwise avoid. Playback streams from the decoded buffer, not from
/// the file.
[[nodiscard]] Result<AudioBuffer> decodeAudioFile(const std::filesystem::path& path,
                                                  const DecodeOptions& options = {});

[[nodiscard]] Result<AudioFileInfo> probeAudioFile(const std::filesystem::path& path);

}  // namespace spectra::audio
