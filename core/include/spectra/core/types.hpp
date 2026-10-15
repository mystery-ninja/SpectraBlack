#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <type_traits>

namespace spectra {

/// Scene time. Always seconds, never frame indices — the project frame rate has
/// to be changeable after the fact without shifting any existing animation.
/// See docs/05-ANIMATION-MODEL.md.
using Seconds = double;

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

/// Linear-space RGBA. Conversion to sRGB happens once, at final output.
using Color = glm::vec4;

/// Whether a value type can be meaningfully interpolated between keyframes.
///
/// Integers and bools are deliberately excluded: they back things like layer
/// indices, blend-mode choices and visibility, where a halfway value is not a
/// value at all. Tracks of non-interpolable types always hold.
template<typename T>
inline constexpr bool kInterpolable = std::is_floating_point_v<T>;

template<glm::length_t N, typename S, glm::qualifier Q>
inline constexpr bool kInterpolable<glm::vec<N, S, Q>> = std::is_floating_point_v<S>;

}  // namespace spectra
