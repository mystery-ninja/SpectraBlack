#pragma once

#include <spectra/core/types.hpp>

#include <span>
#include <vector>

namespace spectra::audio {

/// A detected onset.
struct Beat {
    Seconds time{0.0};

    /// How far the flux at this peak stood above the local threshold, as a
    /// ratio. 1.0 means it only just qualified; a kick in a sparse mix
    /// reaches several. Drivers use it to make a reaction's size follow how
    /// hard the hit was.
    float strength{0.0f};
};

struct OnsetParams {
    /// Width of the median window, centred on the frame being tested.
    ///
    /// Centred is the part that matters, and it is only possible because we
    /// analyse offline: the threshold at 1:04 is allowed to know what happens
    /// at 1:04.25. A realtime detector can only look backwards, which is why
    /// its first onset after a quiet passage is always late.
    Seconds window{0.5};

    /// Multiple of the local median the flux must exceed. Lower finds more.
    float sensitivity{2.0f};

    /// Refractory period. 120 ms admits eighths at 250 BPM and excludes
    /// sixteenths above 125, which is roughly where "a hit you would clap to"
    /// stops and "ornamentation" starts.
    Seconds minimumInterval{0.12};

    /// Flux below this is never an onset, however quiet its neighbourhood.
    /// Without it, a fade-out's noise floor produces a confident beat grid.
    float floor{0.0005f};
};

/// Picks onsets out of a spectral flux envelope.
///
/// **These are onsets, not beats.** On real music the detector finds every
/// hi-hat and ghost note as well as the kick, which is what an onset detector
/// is for and is not the same thing as a beat grid. The defaults below lean
/// towards hits a listener would clap to, but separating the pulse from the
/// ornamentation needs tempo estimation — autocorrelate the envelope, fit a
/// grid — and that is Phase 2 (docs/04-AUDIO-ENGINE.md).
///
/// A free function over the envelope rather than a method on the cache,
/// because the interesting failures — a click train detected at half tempo, a
/// fade-out detected as a drum solo — are much easier to provoke by writing
/// the envelope directly than by synthesising audio that produces it.
///
/// `rate` is the analysis rate in Hz, and `flux[i]` is the value at
/// `i / rate` seconds.
[[nodiscard]] std::vector<Beat>
detectOnsets(std::span<const float> flux, double rate, const OnsetParams& params = {});

}  // namespace spectra::audio
