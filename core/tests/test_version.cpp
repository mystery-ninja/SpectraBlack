// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#include <spectra/core/version.hpp>

#include <doctest/doctest.h>

// MSVC's <string_view> only forward-declares basic_ostream, so stringifying a
// string_view for a failure message does not compile without this. libstdc++
// pulls it in transitively, which is why GCC does not need it.
#include <ostream>

TEST_CASE("version constants are populated from the build system") {
    CHECK(spectra::kVersion.major == 0);
    CHECK(spectra::kVersion.minor == 1);
    CHECK(spectra::kVersion.patch == 0);
}

TEST_CASE("version string matches the version constants") {
    CHECK(spectra::versionString() == "0.1.0");
}
