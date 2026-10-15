#pragma once

#include <spectra/core/types.hpp>

#include <cstdint>

namespace spectra {

/// How a driver's contribution combines with the value underneath it.
///
/// This choice decides whether a driver *decorates* the keyframe animation or
/// *overrides* it, which matters more than it looks. See
/// docs/05-ANIMATION-MODEL.md.
enum class BlendOp : std::uint8_t {
    Add,       ///< `base + shaped * amount`. Shake, offsets, jitter.
    Multiply,  ///< `base * (1 + shaped * amount)`. Pulse — respects the base.
    Replace,   ///< `lerp(base, shaped, amount)`. Pure Avee behaviour.
    Max,       ///< Floor the value.
    Min,       ///< Ceiling the value.
};

/// Combine one scalar channel.
[[nodiscard]] float applyBlend(BlendOp op, float base, float shaped, float amount);

/// Combine a driver's scalar contribution into a value of any property type.
///
/// A driver always produces one number, which is applied to every component.
/// A beat shake on a 2D position shakes both axes, as in Avee. Per-component
/// drivers are a later refinement and do not change this signature.
template<typename T>
[[nodiscard]] T blendInto(BlendOp op, const T& base, float shaped, float amount) {
    if constexpr (kInterpolable<T>) {
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(applyBlend(op, static_cast<float>(base), shaped, amount));
        } else {
            T result = base;
            for (glm::length_t i = 0; i < T::length(); ++i) {
                result[i] = applyBlend(op, base[i], shaped, amount);
            }
            return result;
        }
    } else {
        // Non-interpolable properties (bools, indices) have no sensible
        // arithmetic blend; drivers on them are ignored rather than guessed at.
        (void)op;
        (void)shaped;
        (void)amount;
        return base;
    }
}

}  // namespace spectra
