// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#include <spectra/core/version.hpp>
#include <spectra/platform/clock.hpp>

#include <cstdio>

int main() {
    std::printf("Spectra Black %s (%s)\n",
                spectra::versionString().data(),
                spectra::platform::platformName().data());
    std::printf("No editor yet. See docs/15-BUILD-ORDER.md, Stage 5.\n");
    return 0;
}
