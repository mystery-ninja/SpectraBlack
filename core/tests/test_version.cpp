// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#include <spectra/core/version.hpp>

#include <doctest/doctest.h>

TEST_CASE("version constants are populated from the build system") {
    CHECK(spectra::kVersion.major == 0);
    CHECK(spectra::kVersion.minor == 1);
    CHECK(spectra::kVersion.patch == 0);
}

TEST_CASE("version string matches the version constants") {
    CHECK(spectra::versionString() == "0.1.0");
}
