// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#pragma once

#include <cstdint>
#include <string_view>

namespace spectra {

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;

    friend constexpr bool operator==(const Version&, const Version&) = default;
};

constexpr Version kVersion{SPECTRA_VERSION_MAJOR, SPECTRA_VERSION_MINOR, SPECTRA_VERSION_PATCH};

/// Human-readable version, e.g. "0.1.0".
std::string_view versionString();

}  // namespace spectra
