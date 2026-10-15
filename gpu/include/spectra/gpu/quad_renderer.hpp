#pragma once

#include <spectra/core/result.hpp>
#include <spectra/core/types.hpp>
#include <spectra/gpu/device.hpp>
#include <spectra/gpu/handles.hpp>
#include <spectra/gpu/types.hpp>

#include <memory>

namespace spectra::gpu {

/// A textured or solid rectangle, positioned the way every node in the scene
/// is positioned.
///
/// **Nothing here is in pixels.** `position` is a fraction of the target, and
/// `size` is in units of the target's *smaller* dimension — Avee's `Meter`
/// convention, adopted because it is what makes a scene authored in a 400px
/// preview panel export correctly at 4K. See docs/03-RENDER-ENGINE.md.
struct Quad {
    Vec2 position{0.5F, 0.5F};  ///< (0,0) top-left, (1,1) bottom-right.
    Vec2 size{1.0F, 1.0F};      ///< Fractions of the smaller dimension.
    Vec2 anchor{0.5F, 0.5F};    ///< Point within the quad that `position` places.
    float rotation{0.0F};       ///< Degrees, clockwise.

    /// Multiplied with the texture, or used alone when there is none. Linear
    /// light, not sRGB.
    Color color{1.0F, 1.0F, 1.0F, 1.0F};

    TextureHandle texture;
    BlendMode blend{BlendMode::Alpha};
};

/// Draws quads, and converts the final image to sRGB.
///
/// It lives in `gpu/` rather than in `render/` because the present pass has
/// to exist before any renderer does, and because every renderer above needs
/// the same quad. It owns one vertex buffer and two shaders, and nothing
/// per-draw.
class QuadRenderer {
public:
    [[nodiscard]] static Result<std::unique_ptr<QuadRenderer>> create(Device& device);

    ~QuadRenderer();

    QuadRenderer(const QuadRenderer&) = delete;
    QuadRenderer& operator=(const QuadRenderer&) = delete;
    QuadRenderer(QuadRenderer&&) = delete;
    QuadRenderer& operator=(QuadRenderer&&) = delete;

    /// Tell the renderer the size of what it is drawing into, which is what
    /// the normalized coordinates resolve against.
    void begin(int width, int height);

    void draw(const Quad& quad);

    /// A quad covering the whole target. For backgrounds, and for feeding one
    /// pass's output into the next.
    void drawFullscreen(TextureHandle texture,
                        BlendMode blend = BlendMode::None,
                        const Color& tint = Color{1.0F, 1.0F, 1.0F, 1.0F});

    /// Copy a linear-light texture to the bound target, encoding to sRGB.
    ///
    /// Done in a shader rather than with GL_FRAMEBUFFER_SRGB because export
    /// has no swap chain: the same code has to produce the same bytes whether
    /// it ends up on screen or in a file.
    void present(TextureHandle source, int width, int height);

private:
    QuadRenderer();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spectra::gpu
