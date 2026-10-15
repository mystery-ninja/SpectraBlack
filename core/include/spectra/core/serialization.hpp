#pragma once

#include <spectra/core/result.hpp>
#include <spectra/core/scene.hpp>

#include <memory>
#include <string>

namespace spectra {

/// Current project format version. Bump on a breaking change and add a
/// migration. See docs/06-PROJECT-FORMAT.md.
inline constexpr int kFormatVersion = 1;

inline constexpr std::string_view kFormatTag = "spectra-black-project";

/// Serialize a scene to project JSON.
///
/// Takes a non-const scene because properties are enumerated through
/// `describe()`, which hands out mutable pointers. Nothing is modified.
///
/// `indent` of -1 writes compact JSON; the default produces the readable,
/// diffable form that belongs in a repository.
[[nodiscard]] std::string toJson(Scene& scene, int indent = 2);

/// Parse project JSON.
///
/// Never throws and never partially mutates a caller's scene: either you get
/// a complete new one or a message explaining what was wrong with the input.
[[nodiscard]] Result<std::unique_ptr<Scene>> fromJson(std::string_view json);

}  // namespace spectra
