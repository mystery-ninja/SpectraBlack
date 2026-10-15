#pragma once

#include <spectra/core/driver.hpp>
#include <spectra/core/keyframe.hpp>
#include <spectra/core/types.hpp>

#include <vector>

namespace spectra {

/// An animatable value.
///
/// The whole project turns on this type accepting a keyframe curve and a set
/// of audio drivers *at the same time*:
///
/// | Keyframes | Drivers | Behaves like          |
/// |-----------|---------|-----------------------|
/// | no        | no      | a constant            |
/// | yes       | no      | After Effects         |
/// | no        | yes     | Avee                  |
/// | yes       | yes     | why this app exists   |
///
/// See docs/05-ANIMATION-MODEL.md.
template<typename T>
struct Property {
    T staticValue{};  ///< Used when there are no keyframes.
    KeyframeTrack<T> keyframes;
    std::vector<Driver> drivers;

    Property() = default;

    explicit Property(const T& initial) : staticValue(initial) {}

    [[nodiscard]] bool animated() const { return !keyframes.empty() || !drivers.empty(); }

    /// The value before any driver runs.
    [[nodiscard]] T base(Seconds t) const {
        return keyframes.empty() ? staticValue : keyframes.evaluate(t);
    }

    /// The final value at `t`.
    ///
    /// `signals` may be null, in which case drivers are skipped and only the
    /// keyframe curve applies. That is what the renderer uses for a project
    /// with no audio loaded, and what keyframe tests use to stay independent
    /// of the audio engine.
    [[nodiscard]] T evaluate(Seconds t, const SignalProvider* signals = nullptr) const {
        T value = base(t);
        if (signals == nullptr) {
            return value;
        }
        for (const Driver& driver : drivers) {
            value = driver.applyTo(value, *signals, t);
        }
        return value;
    }
};

using FloatProperty = Property<float>;
using Vec2Property = Property<Vec2>;
using Vec3Property = Property<Vec3>;
using ColorProperty = Property<Color>;
using BoolProperty = Property<bool>;

}  // namespace spectra
