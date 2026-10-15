// The only translation unit in the project that includes libav* headers.
// Everything above it sees decoder.hpp, which mentions no FFmpeg type.
//
// Swapping FFmpeg out — on Android, where it is a 20 MB liability and the
// platform has MediaCodec — means writing a sibling of this file, not touching
// anything else.

#include <spectra/audio/decoder.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <memory>
#include <string>
#include <vector>

namespace spectra::audio {
namespace {

std::string avError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return "unknown libav error " + std::to_string(code);
    }
    return buffer;
}

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};

struct ResamplerDeleter {
    void operator()(SwrContext* resampler) const { swr_free(&resampler); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using ResamplerPtr = std::unique_ptr<SwrContext, ResamplerDeleter>;

/// Opens the file and locates its best audio stream.
struct OpenedFile {
    FormatContextPtr format;
    int streamIndex{-1};
    const AVCodec* codec{nullptr};
};

Result<OpenedFile> openAudioFile(const std::filesystem::path& path) {
    const std::string filename = path.string();

    AVFormatContext* rawFormat = nullptr;
    int status = avformat_open_input(&rawFormat, filename.c_str(), nullptr, nullptr);
    if (status < 0) {
        return Result<OpenedFile>::error("cannot open " + filename + ": " + avError(status));
    }
    FormatContextPtr format(rawFormat);

    status = avformat_find_stream_info(format.get(), nullptr);
    if (status < 0) {
        return Result<OpenedFile>::error("no stream info in " + filename + ": " + avError(status));
    }

    // "Best" is FFmpeg's own heuristic — channel count, then bit rate. It is
    // the right call for a video file carrying both a stereo mix and a 5.1
    // one, and it is why we do not just take the first audio stream.
    const AVCodec* codec = nullptr;
    const int streamIndex =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (streamIndex < 0) {
        return Result<OpenedFile>::error("no audio stream in " + filename + ": " +
                                         avError(streamIndex));
    }
    if (codec == nullptr) {
        return Result<OpenedFile>::error("no decoder for the audio stream in " + filename);
    }

    return Result<OpenedFile>::ok(OpenedFile{
        .format = std::move(format),
        .streamIndex = streamIndex,
        .codec = codec,
    });
}

Result<CodecContextPtr> openDecoder(const OpenedFile& file) {
    const AVStream* stream = file.format->streams[file.streamIndex];

    CodecContextPtr decoder(avcodec_alloc_context3(file.codec));
    if (!decoder) {
        return Result<CodecContextPtr>::error("out of memory allocating a decoder");
    }

    int status = avcodec_parameters_to_context(decoder.get(), stream->codecpar);
    if (status < 0) {
        return Result<CodecContextPtr>::error("bad codec parameters: " + avError(status));
    }

    // Deterministic output is the whole point of this stage
    // (docs/04-AUDIO-ENGINE.md), and thread_count = 0 would let FFmpeg pick a
    // count from the host's core count. Frame-threaded audio decoders are
    // deterministic, but the decision is cheap to remove entirely.
    decoder->thread_count = 1;

    status = avcodec_open2(decoder.get(), file.codec, nullptr);
    if (status < 0) {
        return Result<CodecContextPtr>::error("cannot open decoder: " + avError(status));
    }

    return Result<CodecContextPtr>::ok(std::move(decoder));
}

/// Converts whatever the decoder produces into interleaved float at the rate
/// and channel count the caller asked for.
class Resampler {
public:
    Result<bool> configure(const AVChannelLayout& inputLayout,
                           AVSampleFormat inputFormat,
                           int inputRate,
                           int outputChannels,
                           int outputRate) {
        inputRate_ = inputRate;
        outputRate_ = outputRate;
        outputChannels_ = outputChannels;

        AVChannelLayout outputLayout = {};
        av_channel_layout_default(&outputLayout, outputChannels);

        SwrContext* raw = nullptr;
        const int status = swr_alloc_set_opts2(&raw,
                                               &outputLayout,
                                               AV_SAMPLE_FMT_FLT,
                                               outputRate,
                                               &inputLayout,
                                               inputFormat,
                                               inputRate,
                                               0,
                                               nullptr);
        av_channel_layout_uninit(&outputLayout);
        if (status < 0) {
            return Result<bool>::error("cannot configure the resampler: " + avError(status));
        }

        resampler_.reset(raw);

        // FFmpeg's default stereo-to-mono matrix is 1/sqrt(2) per channel,
        // which preserves power for uncorrelated channels and therefore adds
        // 3 dB to anything mixed to the centre. That is a defensible choice,
        // but it is not the one downmixToMono() makes, and shipping two
        // different definitions of "mono" is how a signal ends up depending
        // on which path produced it. Mono gets a plain average here; wider
        // targets keep FFmpeg's matrix, which is right for playback.
        if (outputChannels == 1) {
            const std::vector<double> matrix(static_cast<std::size_t>(inputLayout.nb_channels),
                                             1.0 / inputLayout.nb_channels);
            const int matrixStatus =
                swr_set_matrix(resampler_.get(), matrix.data(), inputLayout.nb_channels);
            if (matrixStatus < 0) {
                return Result<bool>::error("cannot set the downmix matrix: " +
                                           avError(matrixStatus));
            }
        }

        const int initStatus = swr_init(resampler_.get());
        if (initStatus < 0) {
            return Result<bool>::error("cannot initialise the resampler: " + avError(initStatus));
        }
        return Result<bool>::ok(true);
    }

    /// Appends the converted samples to `out`. A null frame flushes whatever
    /// the resampler is holding back, which for a non-trivial rate conversion
    /// is the last few milliseconds of the track.
    Result<bool> convert(const AVFrame* frame, std::vector<float>& out) {
        const int inputSamples = frame != nullptr ? frame->nb_samples : 0;
        const std::int64_t delay = swr_get_delay(resampler_.get(), inputRate_);
        const std::int64_t capacity =
            av_rescale_rnd(delay + inputSamples, outputRate_, inputRate_, AV_ROUND_UP);
        if (capacity <= 0) {
            return Result<bool>::ok(true);
        }

        const std::size_t offset = out.size();
        out.resize(offset +
                   static_cast<std::size_t>(capacity) * static_cast<std::size_t>(outputChannels_));

        auto* destination = reinterpret_cast<std::uint8_t*>(out.data() + offset);
        const auto** source =
            frame != nullptr ? const_cast<const std::uint8_t**>(frame->extended_data) : nullptr;
        const int produced = swr_convert(
            resampler_.get(), &destination, static_cast<int>(capacity), source, inputSamples);
        if (produced < 0) {
            return Result<bool>::error("resampling failed: " + avError(produced));
        }

        out.resize(offset +
                   static_cast<std::size_t>(produced) * static_cast<std::size_t>(outputChannels_));
        return Result<bool>::ok(true);
    }

private:
    ResamplerPtr resampler_;
    int inputRate_{0};
    int outputRate_{0};
    int outputChannels_{0};
};

}  // namespace

Result<AudioFileInfo> probeAudioFile(const std::filesystem::path& path) {
    Result<OpenedFile> opened = openAudioFile(path);
    if (!opened) {
        return Result<AudioFileInfo>::error(opened.error());
    }
    const OpenedFile& file = opened.value();
    const AVStream* stream = file.format->streams[file.streamIndex];

    // Prefer the stream's own duration: a container duration covers the video
    // track too, and for a stream copied out of a longer file the two disagree.
    Seconds duration = 0.0;
    if (stream->duration != AV_NOPTS_VALUE && stream->time_base.den > 0) {
        duration = static_cast<Seconds>(stream->duration) * av_q2d(stream->time_base);
    } else if (file.format->duration != AV_NOPTS_VALUE) {
        duration = static_cast<Seconds>(file.format->duration) / AV_TIME_BASE;
    }

    return Result<AudioFileInfo>::ok(AudioFileInfo{
        .duration = duration,
        .sampleRate = stream->codecpar->sample_rate,
        .channels = stream->codecpar->ch_layout.nb_channels,
        .codec = file.codec->name,
        .container = file.format->iformat->name,
    });
}

Result<AudioBuffer> decodeAudioFile(const std::filesystem::path& path,
                                    const DecodeOptions& options) {
    Result<OpenedFile> opened = openAudioFile(path);
    if (!opened) {
        return Result<AudioBuffer>::error(opened.error());
    }
    OpenedFile& file = opened.value();

    Result<CodecContextPtr> decoderResult = openDecoder(file);
    if (!decoderResult) {
        return Result<AudioBuffer>::error(decoderResult.error());
    }
    CodecContextPtr decoder = std::move(decoderResult.value());

    const int inputRate = decoder->sample_rate;
    const int inputChannels = decoder->ch_layout.nb_channels;
    if (inputRate <= 0 || inputChannels <= 0) {
        return Result<AudioBuffer>::error(
            "the audio stream declares no sample rate or no channels");
    }

    const int outputRate = options.sampleRate > 0 ? options.sampleRate : inputRate;
    const int outputChannels = options.channels > 0 ? options.channels : inputChannels;

    Resampler resampler;
    if (Result<bool> configured = resampler.configure(
            decoder->ch_layout, decoder->sample_fmt, inputRate, outputChannels, outputRate);
        !configured) {
        return Result<AudioBuffer>::error(configured.error());
    }

    FramePtr frame(av_frame_alloc());
    PacketPtr packet(av_packet_alloc());
    if (!frame || !packet) {
        return Result<AudioBuffer>::error("out of memory allocating decode scratch");
    }

    AudioBuffer buffer{
        .samples = {},
        .sampleRate = outputRate,
        .channels = outputChannels,
    };

    // Reserve against the container's duration. Wrong for a VBR file whose
    // header lies, but only ever by one reallocation.
    if (const Result<AudioFileInfo> info = probeAudioFile(path);
        info && info.value().duration > 0.0) {
        const auto frames = static_cast<std::size_t>(info.value().duration * outputRate);
        buffer.samples.reserve(frames * static_cast<std::size_t>(outputChannels));
    }

    auto drainDecoder = [&]() -> Result<bool> {
        for (;;) {
            const int status = avcodec_receive_frame(decoder.get(), frame.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return Result<bool>::ok(true);
            }
            if (status < 0) {
                return Result<bool>::error("decode failed: " + avError(status));
            }
            Result<bool> converted = resampler.convert(frame.get(), buffer.samples);
            av_frame_unref(frame.get());
            if (!converted) {
                return converted;
            }
        }
    };

    for (;;) {
        const int status = av_read_frame(file.format.get(), packet.get());
        if (status == AVERROR_EOF) {
            break;
        }
        if (status < 0) {
            return Result<AudioBuffer>::error("read failed: " + avError(status));
        }

        if (packet->stream_index == file.streamIndex) {
            const int sendStatus = avcodec_send_packet(decoder.get(), packet.get());
            if (sendStatus < 0 && sendStatus != AVERROR(EAGAIN)) {
                av_packet_unref(packet.get());
                return Result<AudioBuffer>::error("decode failed: " + avError(sendStatus));
            }
            av_packet_unref(packet.get());

            if (Result<bool> drained = drainDecoder(); !drained) {
                return Result<AudioBuffer>::error(drained.error());
            }
        } else {
            av_packet_unref(packet.get());
        }
    }

    // A null packet tells the decoder there is no more input, which matters
    // for codecs that hold frames back; the null frame then flushes the
    // resampler's own tail.
    avcodec_send_packet(decoder.get(), nullptr);
    if (Result<bool> drained = drainDecoder(); !drained) {
        return Result<AudioBuffer>::error(drained.error());
    }
    if (Result<bool> flushed = resampler.convert(nullptr, buffer.samples); !flushed) {
        return Result<AudioBuffer>::error(flushed.error());
    }

    return Result<AudioBuffer>::ok(std::move(buffer));
}

}  // namespace spectra::audio
