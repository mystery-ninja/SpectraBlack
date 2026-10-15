#include <spectra/gpu/types.hpp>

namespace spectra::gpu {

std::uint32_t bytesPerPixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            return 4;
        case TextureFormat::RGBA16F:
            return 8;
        case TextureFormat::R8:
            return 1;
        case TextureFormat::R16F:
            return 2;
        case TextureFormat::Depth24:
            return 4;
    }
    return 0;
}

bool isDepthFormat(TextureFormat format) {
    return format == TextureFormat::Depth24;
}

std::string_view blendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            return "None";
        case BlendMode::Alpha:
            return "Alpha";
        case BlendMode::PreMulAlpha:
            return "PreMulAlpha";
        case BlendMode::Add:
            return "Add";
        case BlendMode::AddAlpha:
            return "AddAlpha";
        case BlendMode::Screen:
            return "Screen";
        case BlendMode::Over:
            return "Over";
    }
    return "Alpha";
}

BlendState blendStateFor(BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            return BlendState{.enabled = false};

        // Straight alpha. The alpha channel uses One rather than SrcAlpha so
        // that compositing into a transparent target accumulates coverage
        // correctly instead of darkening the edges of everything.
        case BlendMode::Alpha:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::SrcAlpha,
                              .colorDst = BlendFactor::OneMinusSrcAlpha,
                              .alphaSrc = BlendFactor::One,
                              .alphaDst = BlendFactor::OneMinusSrcAlpha};

        // Colour is already multiplied by alpha, which is the correct input
        // for anything that has been filtered or blurred.
        case BlendMode::PreMulAlpha:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::One,
                              .colorDst = BlendFactor::OneMinusSrcAlpha,
                              .alphaSrc = BlendFactor::One,
                              .alphaDst = BlendFactor::OneMinusSrcAlpha};

        case BlendMode::Add:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::One,
                              .colorDst = BlendFactor::One,
                              .alphaSrc = BlendFactor::One,
                              .alphaDst = BlendFactor::One};

        case BlendMode::AddAlpha:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::SrcAlpha,
                              .colorDst = BlendFactor::One,
                              .alphaSrc = BlendFactor::One,
                              .alphaDst = BlendFactor::One};

        // dst + src * (1 - dst) in the fixed-function form. Approaches white
        // asymptotically instead of clipping, which is why it reads as a
        // softer glow than Add.
        case BlendMode::Screen:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::One,
                              .colorDst = BlendFactor::OneMinusSrcColor,
                              .alphaSrc = BlendFactor::One,
                              .alphaDst = BlendFactor::OneMinusSrcAlpha};

        // Composites colour but leaves the target's alpha untouched, so
        // drawing into a layer does not punch holes in its coverage.
        case BlendMode::Over:
            return BlendState{.enabled = true,
                              .colorSrc = BlendFactor::SrcAlpha,
                              .colorDst = BlendFactor::OneMinusSrcAlpha,
                              .alphaSrc = BlendFactor::Zero,
                              .alphaDst = BlendFactor::One};
    }
    return BlendState{};
}

}  // namespace spectra::gpu
