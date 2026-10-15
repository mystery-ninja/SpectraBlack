#pragma once

#include <cstdint>

namespace spectra {

/// How a keyframe is approached (`in`) or left (`out`).
///
/// The side the mode is set on already carries the direction, so there is no
/// separate `EaseIn`/`EaseOut`: `Ease` on a keyframe's `out` side is an ease
/// out of it, and on its `in` side an ease into it.
enum class Interpolation : std::uint8_t {
    Hold,    ///< Keep the left value until the next keyframe. Discrete values.
    Linear,  ///< Constant rate across the segment.
    Bezier,  ///< Use the keyframe's explicit handle.
    Ease,    ///< Flat tangent on this side. Equivalent to AE's Easy Ease.
};

/// One side of a segment's timing curve, in normalized segment space: `x` is a
/// fraction of the segment's duration, `y` a fraction of its value change.
///
/// Both handles are stored as an offset *from their own keyframe, pointing into
/// the segment*, so the two sides are mirror images of each other and the
/// defaults are symmetric.
///
/// `x` is clamped to [0, 1] when the curve is solved. That is what guarantees
/// the curve stays a function of time — an unclamped handle can fold the curve
/// back on itself, giving one time two values. `y` is left unclamped, because
/// overshoot past the keyframe value is exactly how anticipation and bounce are
/// built.
struct BezierHandle {
    float x{1.0f / 3.0f};
    float y{1.0f / 3.0f};

    friend constexpr bool operator==(const BezierHandle&, const BezierHandle&) = default;
};

/// The handle that produces an exactly linear segment when used on both sides.
inline constexpr BezierHandle kLinearHandle{1.0f / 3.0f, 1.0f / 3.0f};

/// The handle that produces a flat tangent — AE's Easy Ease.
inline constexpr BezierHandle kEaseHandle{0.42f, 0.0f};

/// The handle a given mode contributes, falling back to `explicitHandle` for
/// `Bezier`. `Hold` never reaches the curve solver and returns the linear handle.
constexpr BezierHandle handleFor(Interpolation mode, BezierHandle explicitHandle) {
    switch (mode) {
        case Interpolation::Bezier:
            return explicitHandle;
        case Interpolation::Ease:
            return kEaseHandle;
        case Interpolation::Linear:
        case Interpolation::Hold:
            break;
    }
    return kLinearHandle;
}

/// Solve a cubic bezier segment for `y` at normalized time `x`.
///
/// `outHandle` belongs to the left keyframe and `inHandle` to the right one,
/// both as offsets from their own keyframe. Control points are therefore
/// P0 = (0,0), P1 = outHandle, P2 = (1,1) - inHandle, P3 = (1,1).
float solveBezier(BezierHandle outHandle, BezierHandle inHandle, float x);

}  // namespace spectra
