// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#include <spectra/core/version.hpp>

#define SPECTRA_STR_INNER(x) #x
#define SPECTRA_STR(x) SPECTRA_STR_INNER(x)

namespace spectra {

std::string_view versionString() {
    return SPECTRA_STR(SPECTRA_VERSION_MAJOR) "." SPECTRA_STR(
        SPECTRA_VERSION_MINOR) "." SPECTRA_STR(SPECTRA_VERSION_PATCH);
}

}  // namespace spectra
