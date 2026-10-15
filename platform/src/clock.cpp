// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#include <spectra/platform/clock.hpp>

#include <chrono>

namespace spectra::platform {

double monotonicSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    const std::chrono::duration<double> elapsed = Clock::now() - start;
    return elapsed.count();
}

}  // namespace spectra::platform
