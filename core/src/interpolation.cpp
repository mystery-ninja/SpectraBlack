#include <spectra/core/interpolation.hpp>

#include <algorithm>
#include <cmath>

namespace spectra {
namespace {

constexpr int kNewtonIterations = 8;
constexpr int kBisectionIterations = 32;
constexpr float kNewtonMinSlope = 1e-5f;
constexpr float kSolveEpsilon = 1e-6f;

/// Cubic bezier component with P0 = 0 and P3 = 1.
constexpr float bezierAxis(float c1, float c2, float u) {
    const float v = 1.0f - u;
    return 3.0f * v * v * u * c1 + 3.0f * v * u * u * c2 + u * u * u;
}

constexpr float bezierAxisSlope(float c1, float c2, float u) {
    const float v = 1.0f - u;
    return 3.0f * v * v * c1 + 6.0f * v * u * (c2 - c1) + 3.0f * u * u * (1.0f - c2);
}

/// Invert x(u) to recover the curve parameter for a given time.
float solveParameterForX(float x1, float x2, float x) {
    float u = x;

    for (int i = 0; i < kNewtonIterations; ++i) {
        const float error = bezierAxis(x1, x2, u) - x;
        if (std::abs(error) < kSolveEpsilon) {
            return u;
        }
        const float slope = bezierAxisSlope(x1, x2, u);
        if (std::abs(slope) < kNewtonMinSlope) {
            break;
        }
        u -= error / slope;
    }

    // Newton can leave the unit interval on near-flat curves; bisection cannot.
    float low = 0.0f;
    float high = 1.0f;
    u = std::clamp(u, low, high);
    for (int i = 0; i < kBisectionIterations; ++i) {
        const float value = bezierAxis(x1, x2, u);
        if (std::abs(value - x) < kSolveEpsilon) {
            break;
        }
        if (value < x) {
            low = u;
        } else {
            high = u;
        }
        u = 0.5f * (low + high);
    }
    return u;
}

}  // namespace

float solveBezier(BezierHandle outHandle, BezierHandle inHandle, float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }

    const float x1 = std::clamp(outHandle.x, 0.0f, 1.0f);
    const float x2 = 1.0f - std::clamp(inHandle.x, 0.0f, 1.0f);
    const float y1 = outHandle.y;
    const float y2 = 1.0f - inHandle.y;

    // Evenly spaced collinear control points make the curve the identity. This
    // is the overwhelmingly common case, and short-circuiting it keeps linear
    // segments exact rather than merely within solver tolerance.
    if (outHandle == kLinearHandle && inHandle == kLinearHandle) {
        return x;
    }

    const float u = solveParameterForX(x1, x2, x);
    return bezierAxis(y1, y2, u);
}

}  // namespace spectra
