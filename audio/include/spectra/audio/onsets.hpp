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
    float sensitivity{1.6f};

    /// Refractory period. 100 ms caps detection at 600 BPM, which is well
    /// past any real tempo and still admits sixteenths at 150.
    Seconds minimumInterval{0.1};

    /// Flux below this is never an onset, however quiet its neighbourhood.
    /// Without it, a fade-out's noise floor produces a confident beat grid.
    float floor{0.0005f};
};

/// Picks onsets out of a spectral flux envelope.
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
