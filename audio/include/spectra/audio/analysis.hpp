#pragma once

#include <spectra/audio/buffer.hpp>
#include <spectra/audio/dsp.hpp>
#include <spectra/audio/onsets.hpp>
#include <spectra/core/result.hpp>
#include <spectra/core/types.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace spectra::audio {

/// Where the band groups divide, following docs/04-AUDIO-ENGINE.md.
inline constexpr float kBassMaxHz = 250.0f;
inline constexpr float kMidMaxHz = 4000.0f;

/// What "1.0" means for a band value.
enum class Normalization : std::uint8_t {
    /// Nothing. Magnitudes come out at whatever scale the window and the
    /// source level produce, which is right if something downstream is going
    /// to do its own scaling.
    None,

    /// One gain for the whole track, from the whole track's statistics. A
    /// quiet master does not produce tiny bars and a loud one does not clip
    /// everything flat, and the spectrum keeps its shape. The default.
    ///
    /// This is the thing no realtime player can do: the intro is scaled
    /// against the song it belongs to rather than against the last two
    /// seconds, so it is not crushed by the chorus.
    Track,

    /// A separate gain per band, so every band reaches 1.0 at its own loudest
    /// moment. Maximally lively, and it throws the spectral shape away
    /// entirely — at a peak, every bar is the same height. Some people want
    /// exactly that.
    PerBand,
};

/// Attack and decay for one band group, in seconds.
///
/// Per group rather than global because of the third cause of the Avee bass
/// bug: with one decay coefficient the bass band's sustained energy never
/// falls. Bass is set to fall faster than the highs by default.
struct EnvelopeTimes {
    Seconds attack{0.008};
    Seconds decay{0.120};
};

struct AnalysisParams {
    /// The rate the audio must be decoded at. Fixed rather than inherited
    /// from the file so that `windowSize` means a fixed number of
    /// milliseconds, and so that two copies of a track at different rates
    /// analyze identically.
    int sampleRate{44100};

    /// Analysis frames per second. Deliberately well above any project frame
    /// rate, so frame values interpolate a denser signal rather than
    /// extrapolating a sparser one.
    double rate{200.0};

    /// 2048 at 44.1 kHz is 46 ms, the usual compromise between frequency
    /// resolution and time smearing.
    int windowSize{2048};

    int bandCount{256};
    float minHz{20.0f};
    float maxHz{20000.0f};
    BandScale scale{BandScale::Mel};
    WindowShape window{WindowShape::Hann};
    SpectralTilt tilt{SpectralTilt::Pink};

    /// Radius of the across-band Hamming kernel. 0 disables it.
    int smoothingRadius{2};

    Normalization normalization{Normalization::Track};

    /// Manual trim applied after normalization.
    float gain{1.0f};

    EnvelopeTimes bass{.attack = 0.004, .decay = 0.070};
    EnvelopeTimes mid{.attack = 0.008, .decay = 0.120};
    EnvelopeTimes high{.attack = 0.008, .decay = 0.160};

    OnsetParams onsets;

    /// Time constant of the `beatPulse` signal: how long one detected beat
    /// keeps driving something after it happens.
    Seconds pulseDecay{0.18};

    /// Workers for the parallel phase. 0 asks the machine.
    ///
    /// Changing this cannot change the result. The pass is split so that
    /// everything parallel is a pure function of one frame's samples, and
    /// everything order-dependent runs afterwards on one thread.
    int threadCount{0};

    [[nodiscard]] Result<bool> validate() const;

    /// Identifies the parameters for cache keying. Covers everything that
    /// affects the output and nothing that does not — notably not
    /// `threadCount`.
    [[nodiscard]] std::uint64_t hash() const;
};

/// The scalars stored for each analysis frame. Bands live separately, in one
/// flat array, because 48,000 frames of `std::vector<float>` is 48,000
/// allocations for no reason.
struct FrameSummary {
    float rms{0.0f};   ///< Windowed loudness, in sample units.
    float peak{0.0f};  ///< Largest absolute sample in the hop.
    float bass{0.0f};  ///< Mean band value below 250 Hz.
    float mid{0.0f};   ///< 250 Hz to 4 kHz.
    float high{0.0f};  ///< Above 4 kHz.

    /// Spectral flux: the sum of positive band-to-band-over-time changes.
    /// Raw onset strength, before any thresholding.
    float flux{0.0f};

    /// The same thing scaled against the track's own flux distribution, so
    /// that 1.0 is "about as much as ever changes at once in this song".
    /// Continuous, unlike the discrete beats — useful for driving something
    /// by how busy the music is rather than by its pulse.
    float beat{0.0f};

    std::uint16_t peakBand{0};
};

/// A summary read at an arbitrary time, so `peakBand` is fractional.
struct AnalysisSample {
    float rms{0.0f};
    float peak{0.0f};
    float bass{0.0f};
    float mid{0.0f};
    float high{0.0f};
    float flux{0.0f};
    float beat{0.0f};
    float peakBand{0.0f};  ///< Normalized to 0..1 across the band count.
};

/// The analysed track: every frame's bands and summary, queryable by time.
///
/// Reads are const and allocation-free, because the evaluator calls them once
/// per driver per property per frame.
class AnalysisCache {
public:
    [[nodiscard]] const AnalysisParams& params() const { return params_; }

    [[nodiscard]] const BandLayout& layout() const { return layout_; }

    [[nodiscard]] int frameCount() const { return frameCount_; }

    [[nodiscard]] int bandCount() const { return params_.bandCount; }

    [[nodiscard]] double rate() const { return params_.rate; }

    [[nodiscard]] Seconds duration() const { return duration_; }

    [[nodiscard]] bool empty() const { return frameCount_ == 0; }

    /// Bands for one frame. Out-of-range indices clamp to the ends rather
    /// than reading past them.
    [[nodiscard]] std::span<const float> bandsAtFrame(int frame) const;

    [[nodiscard]] const FrameSummary& summaryAtFrame(int frame) const;

    /// Fractional frame position of `t`, clamped into the track.
    [[nodiscard]] double frameIndexAt(Seconds t) const;

    /// One band, interpolated between the two frames either side of `t`.
    [[nodiscard]] float band(int index, Seconds t) const;

    /// Mean of the bands overlapping `[lowHz, highHz)`, interpolated in time.
    [[nodiscard]] float bandRange(float lowHz, float highHz, Seconds t) const;

    [[nodiscard]] AnalysisSample at(Seconds t) const;

    [[nodiscard]] std::span<const Beat> beats() const { return beats_; }

    /// Index of the beat containing `t`, counting from 0 at the first beat
    /// and negative before it.
    ///
    /// An index rather than a time because it seeds the shaping chain's
    /// random stage: a random value has to stay put for the whole beat and
    /// has to be the same when you scrub back to it.
    [[nodiscard]] std::int64_t beatIndexAt(Seconds t) const;

    /// A spike at each beat, decaying with `params.pulseDecay`, scaled by how
    /// hard the hit was. The signal most reactions actually want.
    [[nodiscard]] float beatPulseAt(Seconds t) const;

    /// Position within the current beat, ramping 0 to 1.
    ///
    /// The one Avee has no equivalent for, and the reason it cannot do
    /// motion that is locked to the grid rather than merely twitching at it.
    /// Before the first beat, and after the last, it keeps running on the
    /// nearest known interval rather than freezing.
    [[nodiscard]] float beatPhaseAt(Seconds t) const;

private:
    friend Result<AnalysisCache> analyze(const AudioBuffer&, const AnalysisParams&);
    friend Result<bool>
    writeAnalysisCache(const std::filesystem::path&, const AnalysisCache&, std::uint64_t);
    friend Result<AnalysisCache>
    readAnalysisCache(const std::filesystem::path&, const AnalysisParams&, std::uint64_t);

    AnalysisParams params_;
    BandLayout layout_;
    std::vector<float> bands_;  ///< frameCount_ * bandCount, frame-major.
    std::vector<FrameSummary> summaries_;
    std::vector<Beat> beats_;
    int frameCount_{0};
    Seconds duration_{0.0};
};

/// Analyses a decoded track.
///
/// `audio.sampleRate` must equal `params.sampleRate`; this is an error rather
/// than a silent resample, because an analysis that quietly ran at the wrong
/// rate produces plausible-looking values at the wrong frequencies.
[[nodiscard]] Result<AnalysisCache> analyze(const AudioBuffer& audio,
                                            const AnalysisParams& params = {});

/// Decodes at the right rate and analyses. The usual entry point.
[[nodiscard]] Result<AnalysisCache> analyzeFile(const std::filesystem::path& path,
                                                const AnalysisParams& params = {});

// Declared here so that AnalysisCache can befriend them. Defined in
// cache_file.hpp, which is where anyone looking for them would look.
[[nodiscard]] Result<bool> writeAnalysisCache(const std::filesystem::path& path,
                                              const AnalysisCache& cache,
                                              std::uint64_t audioHash);
[[nodiscard]] Result<AnalysisCache> readAnalysisCache(const std::filesystem::path& path,
                                                      const AnalysisParams& params,
                                                      std::uint64_t audioHash);

}  // namespace spectra::audio
