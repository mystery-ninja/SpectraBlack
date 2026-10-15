// Placement and colour-space tests for the quad renderer. See gl_fixture.hpp.

#include "gl_fixture.hpp"

#include <spectra/gpu/quad_renderer.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;
using spectra::gpu::testing::gl;

namespace {

/// Pixels of a target, bottom row first as GL reads them.
class Readback {
public:
    Readback(Device& device, FramebufferHandle target, int width, int height)
        : width_(width), height_(height),
          pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0.0F) {
        device.readPixels(target, pixels_);
    }

    /// Row 0 is the top of the image, flipping GL's convention so the
    /// expectations in these tests read the way the picture looks.
    [[nodiscard]] Color at(int x, int y) const {
        const auto row = static_cast<std::size_t>(height_ - 1 - y);
        const auto at = (row * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4;
        return Color{pixels_[at], pixels_[at + 1], pixels_[at + 2], pixels_[at + 3]};
    }

    /// How many pixels the quad covered, counting anything not left black.
    [[nodiscard]] int covered() const {
        int count = 0;
        for (std::size_t at = 0; at < pixels_.size(); at += 4) {
            if (pixels_[at] > 0.5F) {
                ++count;
            }
        }
        return count;
    }

private:
    int width_;
    int height_;
    std::vector<float> pixels_;
};

struct Scene {
    std::unique_ptr<QuadRenderer> quads;
    FramebufferHandle target;
    int width{0};
    int height{0};
};

[[nodiscard]] Scene makeScene(Device& device, int width, int height) {
    Scene scene;
    scene.width = width;
    scene.height = height;

    auto quads = QuadRenderer::create(device);
    REQUIRE(quads);
    scene.quads = std::move(quads.value());

    auto target = device.createFramebuffer(
        FramebufferDesc{.width = width, .height = height, .format = TextureFormat::RGBA16F});
    REQUIRE(target);
    scene.target = target.value();

    device.bindFramebuffer(scene.target);
    device.clear(Color{0.0F, 0.0F, 0.0F, 1.0F});
    scene.quads->begin(width, height);
    return scene;
}

void tearDown(Device& device, Scene& scene) {
    device.bindFramebuffer(FramebufferHandle{});
    device.destroyFramebuffer(scene.target);
    scene.quads.reset();
}

}  // namespace

TEST_CASE("a quad's size is measured against the smaller dimension") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    // The same quad in a wide target and a tall one. Resolution independence
    // means it covers the same number of pixels in both, which is what lets
    // a scene authored in a small preview panel export at 4K unchanged.
    const auto coveredIn = [&device](int width, int height) {
        Scene scene = makeScene(device, width, height);

        Quad quad;
        quad.position = Vec2{0.5F, 0.5F};
        quad.size = Vec2{0.5F, 0.5F};
        quad.color = Color{1.0F, 1.0F, 1.0F, 1.0F};
        quad.blend = BlendMode::None;
        scene.quads->draw(quad);

        const int covered = Readback(device, scene.target, width, height).covered();
        tearDown(device, scene);
        return covered;
    };

    // 0.5 of the smaller dimension, which is 32 in both cases, so 16x16.
    CHECK(coveredIn(64, 32) == 16 * 16);
    CHECK(coveredIn(32, 64) == 16 * 16);
}

TEST_CASE("position and anchor place the quad where they say") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    Scene scene = makeScene(device, 32, 32);

    Quad quad;
    quad.position = Vec2{0.0F, 0.0F};
    quad.anchor = Vec2{0.0F, 0.0F};  // Top-left of the quad at the top-left of the target.
    quad.size = Vec2{0.5F, 0.5F};
    quad.color = Color{1.0F, 1.0F, 1.0F, 1.0F};
    quad.blend = BlendMode::None;
    scene.quads->draw(quad);

    const Readback pixels(device, scene.target, 32, 32);
    CHECK(pixels.at(1, 1).r == doctest::Approx(1.0F).epsilon(0.01));    // Inside.
    CHECK(pixels.at(14, 14).r == doctest::Approx(1.0F).epsilon(0.01));  // Still inside.
    CHECK(pixels.at(20, 20).r == doctest::Approx(0.0F).epsilon(0.01));  // Past it.
    CHECK(pixels.at(31, 31).r == doctest::Approx(0.0F).epsilon(0.01));  // Opposite corner.

    tearDown(device, scene);
}

TEST_CASE("a centred anchor centres the quad") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    Scene scene = makeScene(device, 32, 32);

    Quad quad;
    quad.size = Vec2{0.5F, 0.5F};
    quad.color = Color{1.0F, 1.0F, 1.0F, 1.0F};
    quad.blend = BlendMode::None;
    scene.quads->draw(quad);

    const Readback pixels(device, scene.target, 32, 32);
    CHECK(pixels.at(16, 16).r == doctest::Approx(1.0F).epsilon(0.01));
    CHECK(pixels.at(2, 16).r == doctest::Approx(0.0F).epsilon(0.01));
    CHECK(pixels.at(29, 16).r == doctest::Approx(0.0F).epsilon(0.01));

    tearDown(device, scene);
}

TEST_CASE("rotation turns the quad without changing how much it covers") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    const auto coveredAt = [&device](float degrees) {
        Scene scene = makeScene(device, 64, 64);

        Quad quad;
        quad.size = Vec2{0.5F, 0.5F};
        quad.rotation = degrees;
        quad.color = Color{1.0F, 1.0F, 1.0F, 1.0F};
        quad.blend = BlendMode::None;
        scene.quads->draw(quad);

        const int covered = Readback(device, scene.target, 64, 64).covered();
        tearDown(device, scene);
        return covered;
    };

    const int upright = coveredAt(0.0F);
    CHECK(upright == 32 * 32);

    // A quarter turn lands on the pixel grid again, so the area is exact.
    CHECK(coveredAt(90.0F) == upright);

    // At 45 degrees the same square covers the same area, give or take the
    // pixels the edges now cut through.
    CHECK(coveredAt(45.0F) == doctest::Approx(upright).epsilon(0.1));
}

TEST_CASE("a fullscreen quad covers every pixel whatever the aspect ratio") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    Scene scene = makeScene(device, 48, 16);
    scene.quads->drawFullscreen(TextureHandle{}, BlendMode::None, Color{1.0F, 1.0F, 1.0F, 1.0F});

    CHECK(Readback(device, scene.target, 48, 16).covered() == 48 * 16);

    tearDown(device, scene);
}

TEST_CASE("present encodes linear light to sRGB") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto quads = QuadRenderer::create(device);
    REQUIRE(quads);

    auto linear = device.createFramebuffer(
        FramebufferDesc{.width = 8, .height = 8, .format = TextureFormat::RGBA16F});
    REQUIRE(linear);
    auto screen = device.createFramebuffer(
        FramebufferDesc{.width = 8, .height = 8, .format = TextureFormat::RGBA8});
    REQUIRE(screen);

    device.bindFramebuffer(linear.value());
    device.clear(Color{0.5F, 0.0F, 1.0F, 1.0F});

    device.bindFramebuffer(screen.value());
    quads.value()->present(device.framebufferTexture(linear.value()), 8, 8);

    // sRGB encoding of 0.5 linear is 0.7354, not 0.5 and not 0.73 from a
    // plain 2.2 gamma. Getting this wrong is why Avee's gradients band.
    const Readback pixels(device, screen.value(), 8, 8);
    CHECK(pixels.at(4, 4).r == doctest::Approx(0.7354F).epsilon(0.01));
    CHECK(pixels.at(4, 4).g == doctest::Approx(0.0F).epsilon(0.01));
    CHECK(pixels.at(4, 4).b == doctest::Approx(1.0F).epsilon(0.01));

    device.bindFramebuffer(FramebufferHandle{});
    device.destroyFramebuffer(screen.value());
    device.destroyFramebuffer(linear.value());
}

TEST_CASE("an sRGB source texture is sampled as linear light") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    // 188 is roughly 0.5 in linear light once the sRGB curve is undone.
    constexpr std::array<std::uint8_t, 4> kPixel{188, 188, 188, 255};

    auto texture = device.createTexture(TextureDesc{.width = 1,
                                                    .height = 1,
                                                    .format = TextureFormat::SRGB8,
                                                    .filter = TextureFilter::Nearest,
                                                    .pixels = kPixel.data()});
    REQUIRE(texture);

    Scene scene = makeScene(device, 8, 8);
    scene.quads->drawFullscreen(texture.value(), BlendMode::None);

    const Readback pixels(device, scene.target, 8, 8);
    CHECK(pixels.at(4, 4).r == doctest::Approx(0.5F).epsilon(0.02));

    tearDown(device, scene);
    device.destroyTexture(texture.value());
}
