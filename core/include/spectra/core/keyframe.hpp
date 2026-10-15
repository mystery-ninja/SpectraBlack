#pragma once

#include <spectra/core/interpolation.hpp>
#include <spectra/core/types.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace spectra {

template<typename T>
struct Keyframe {
    Seconds time{0.0};
    T value{};
    Interpolation in{Interpolation::Bezier};   ///< How the curve arrives here.
    Interpolation out{Interpolation::Bezier};  ///< How the curve leaves here.
    BezierHandle handleIn{};                   ///< Used when `in == Bezier`.
    BezierHandle handleOut{};                  ///< Used when `out == Bezier`.
};

/// A sorted sequence of keyframes for one property.
///
/// Outside the keyframed range the track holds the nearest value rather than
/// extrapolating, matching After Effects. Extrapolation is a per-property
/// option in AE and can be added later without changing stored data.
template<typename T>
class KeyframeTrack {
public:
    [[nodiscard]] bool empty() const { return keys_.empty(); }

    [[nodiscard]] std::size_t size() const { return keys_.size(); }

    [[nodiscard]] const std::vector<Keyframe<T>>& keys() const { return keys_; }

    [[nodiscard]] const Keyframe<T>& operator[](std::size_t i) const { return keys_[i]; }

    /// Insert keeping the sequence sorted by time. A keyframe at an existing
    /// time replaces it, so dragging one onto another behaves like every other
    /// animation tool rather than silently creating a duplicate.
    void insert(const Keyframe<T>& key) {
        const auto at = std::lower_bound(
            keys_.begin(), keys_.end(), key.time, [](const Keyframe<T>& candidate, Seconds time) {
                return candidate.time < time;
            });

        if (at != keys_.end() && at->time == key.time) {
            *at = key;
            return;
        }
        keys_.insert(at, key);
    }

    void removeAt(std::size_t index) {
        if (index < keys_.size()) {
            keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void clear() { keys_.clear(); }

    [[nodiscard]] T evaluate(Seconds t) const {
        if (keys_.empty()) {
            return T{};
        }
        if (t <= keys_.front().time) {
            return keys_.front().value;
        }
        if (t >= keys_.back().time) {
            return keys_.back().value;
        }

        const auto right = std::lower_bound(
            keys_.begin(), keys_.end(), t, [](const Keyframe<T>& candidate, Seconds time) {
                return candidate.time < time;
            });
        const auto left = right - 1;

        if (left->out == Interpolation::Hold || !kInterpolable<T>) {
            return left->value;
        }

        const Seconds span = right->time - left->time;
        if (span <= 0.0) {
            return right->value;
        }

        const auto x = static_cast<float>((t - left->time) / span);
        const float u = solveBezier(
            handleFor(left->out, left->handleOut), handleFor(right->in, right->handleIn), x);

        return blendValues(left->value, right->value, u);
    }

private:
    /// Split out so the non-interpolable instantiation never has to compile the
    /// arithmetic, which would not be valid for every T a property can hold.
    static T blendValues(const T& a, const T& b, float u) {
        if constexpr (kInterpolable<T>) {
            return static_cast<T>(a + (b - a) * u);
        } else {
            return u < 1.0f ? a : b;
        }
    }

    std::vector<Keyframe<T>> keys_;
};

}  // namespace spectra
