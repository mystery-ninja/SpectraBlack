#include <spectra/core/blend.hpp>

#include <algorithm>

namespace spectra {

float applyBlend(BlendOp op, float base, float shaped, float amount) {
    switch (op) {
        case BlendOp::Add:
            return base + shaped * amount;
        case BlendOp::Multiply:
            return base * (1.0f + shaped * amount);
        case BlendOp::Replace:
            return base + (shaped - base) * amount;
        case BlendOp::Max:
            return std::max(base, shaped * amount);
        case BlendOp::Min:
            return std::min(base, shaped * amount);
    }
    return base;
}

}  // namespace spectra
