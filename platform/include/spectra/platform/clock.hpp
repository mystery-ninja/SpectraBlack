// Spectra Black — https://github.com/mystery-ninja/SpectraBlack
// Licensed under GPL-3.0. See LICENSE at the repository root.

#pragma once

#include <string_view>

namespace spectra::platform {

/// Monotonic seconds since process start. Never goes backwards.
///
/// This is the wall clock used for preview. Export deliberately does not use
/// it -- see docs/03-RENDER-ENGINE.md on why export runs on a virtual clock.
double monotonicSeconds();

/// Short name of the host platform, for logs and diagnostics.
std::string_view platformName();

}  // namespace spectra::platform
