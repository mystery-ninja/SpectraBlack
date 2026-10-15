// Device tests that need a real GL 4.3 context. See gl_fixture.hpp.

#include "gl_fixture.hpp"

#include <spectra/gpu/device.hpp>
#include <spectra/gpu/framegraph.hpp>
#include <spectra/gpu/shader_source.hpp>
#include <spectra/gpu/target_pool.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;
using spectra::gpu::testing::gl;

namespace {

constexpr std::string_view kVertexSource = R"(
layout(location = 0) in vec2 aPosition;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentSource = R"(
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

[[nodiscard]] std::string build(std::string_view name, std::string_view source) {
    const auto processed = preprocessShader(name, source);
    REQUIRE(processed);
    return processed.value().source;
}

/// A full-screen quad, which is all these tests need to draw.
struct Quad {
    BufferHandle vertices;
    VertexLayout layout;
};

[[nodiscard]] Quad makeQuad(Device& device) {
    static constexpr std::array<float, 8> kCorners{
        -1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F};

    auto buffer = device.createBuffer(BufferDesc{.type = BufferType::Vertex,
                                                 .update = BufferUpdate::Static,
                                                 .size = sizeof(kCorners),
                                                 .data = kCorners.data()});
    REQUIRE(buffer);

    Quad quad;
    quad.vertices = buffer.value();
    quad.layout.stride = sizeof(float) * 2;
    quad.layout.attributes = {VertexAttribute{
        .location = 0, .components = 2, .type = AttributeType::Float32, .offset = 0}};
    return quad;
}

/// Colour of the first pixel of a target.
[[nodiscard]] Color firstPixel(Device& device, FramebufferHandle target, int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width * height * 4), 0.0F);
    device.readPixels(target, pixels);
    return Color{pixels[0], pixels[1], pixels[2], pixels[3]};
}

}  // namespace

TEST_CASE("the device reports the context it was given") {
    SPECTRA_REQUIRE_GL();

    CHECK_FALSE(gl().device->info().renderer.empty());
    CHECK_FALSE(gl().device->info().version.empty());
    CHECK(gl().device->info().maxTextureSize >= 2048);
}

TEST_CASE("a handle stops resolving once its resource is destroyed") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto texture =
        device.createTexture(TextureDesc{.width = 16, .height = 8, .format = TextureFormat::RGBA8});
    REQUIRE(texture);
    CHECK(device.textureWidth(texture.value()) == 16);
    CHECK(device.textureHeight(texture.value()) == 8);

    const TextureHandle stale = texture.value();
    device.destroyTexture(stale);

    // The slot may already have been handed to something else; the generation
    // counter is what stops that from resolving to the wrong texture.
    CHECK(device.textureWidth(stale) == 0);
}

TEST_CASE("a texture bigger than the driver allows is refused with a message") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    const int tooBig = device.info().maxTextureSize + 1;
    const auto texture = device.createTexture(TextureDesc{.width = tooBig, .height = 16});

    REQUIRE_FALSE(texture);
    CHECK(texture.error().find("maximum") != std::string::npos);
}

TEST_CASE("buffers report their size and refuse writes that would overrun") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto buffer = device.createBuffer(
        BufferDesc{.type = BufferType::Vertex, .update = BufferUpdate::Dynamic, .size = 64});
    REQUIRE(buffer);
    CHECK(device.bufferSize(buffer.value()) == 64);

    const std::array<std::byte, 16> data{};
    device.updateBuffer(buffer.value(), data, 0);
    device.updateBuffer(buffer.value(), data, 56);  // Would write past the end.

    CHECK(device.takeErrors().empty());
    device.destroyBuffer(buffer.value());
}

TEST_CASE("a shader that does not compile reports the driver's log") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    const auto shader = device.createShader(
        build("v", kVertexSource), build("f", "void main() { this is not glsl }"), "broken.frag");

    REQUIRE_FALSE(shader);
    CHECK(shader.error().find("broken.frag") != std::string::npos);
    CHECK(shader.error().find("fragment") != std::string::npos);
}

TEST_CASE("a framebuffer hands back a colour texture of the size asked for") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto target = device.createFramebuffer(
        FramebufferDesc{.width = 32, .height = 24, .format = TextureFormat::RGBA16F});
    REQUIRE(target);

    const TextureHandle color = device.framebufferTexture(target.value());
    CHECK(color.valid());
    CHECK(device.textureWidth(color) == 32);
    CHECK(device.textureHeight(color) == 24);

    device.destroyFramebuffer(target.value());

    // Destroying the target takes its attachment with it.
    CHECK(device.textureWidth(color) == 0);
}

TEST_CASE("clearing writes linear values, not sRGB-encoded ones") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto target = device.createFramebuffer(FramebufferDesc{.width = 4, .height = 4});
    REQUIRE(target);

    device.bindFramebuffer(target.value());
    device.clear(Color{0.25F, 0.5F, 0.75F, 1.0F});

    const Color pixel = firstPixel(device, target.value(), 4, 4);
    CHECK(pixel.r == doctest::Approx(0.25F).epsilon(0.001));
    CHECK(pixel.g == doctest::Approx(0.5F).epsilon(0.001));
    CHECK(pixel.b == doctest::Approx(0.75F).epsilon(0.001));

    device.bindFramebuffer(FramebufferHandle{});
    device.destroyFramebuffer(target.value());
}

TEST_CASE("the blend modes composite the way the table says they do") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto shader = device.createShader(
        build("quad.vert", kVertexSource), build("quad.frag", kFragmentSource), "quad");
    REQUIRE(shader);

    const Quad quad = makeQuad(device);

    auto target = device.createFramebuffer(FramebufferDesc{.width = 4, .height = 4});
    REQUIRE(target);
    device.bindFramebuffer(target.value());

    const DrawCall call{.shader = shader.value(),
                        .vertices = quad.vertices,
                        .layout = &quad.layout,
                        .primitive = Primitive::TriangleStrip,
                        .count = 4,
                        .instanceCount = 1};

    const auto drawOver = [&](const Color& background, BlendMode mode, const Color& source) {
        device.clear(background);
        device.setBlendMode(mode);
        device.setUniform(shader.value(), "uColor", Vec4(source));
        device.draw(call);
        return firstPixel(device, target.value(), 4, 4);
    };

    SUBCASE("None overwrites whatever was there") {
        const Color result = drawOver(
            Color{1.0F, 0.0F, 0.0F, 1.0F}, BlendMode::None, Color{0.0F, 0.25F, 0.0F, 0.5F});
        CHECK(result.r == doctest::Approx(0.0F).epsilon(0.01));
        CHECK(result.g == doctest::Approx(0.25F).epsilon(0.01));
        CHECK(result.a == doctest::Approx(0.5F).epsilon(0.01));
    }

    SUBCASE("Alpha interpolates towards the source") {
        const Color result = drawOver(
            Color{0.0F, 0.0F, 0.0F, 1.0F}, BlendMode::Alpha, Color{1.0F, 0.0F, 0.0F, 0.5F});
        CHECK(result.r == doctest::Approx(0.5F).epsilon(0.01));
    }

    SUBCASE("PreMulAlpha takes the source colour as-is") {
        const Color result = drawOver(
            Color{0.0F, 0.0F, 0.0F, 1.0F}, BlendMode::PreMulAlpha, Color{0.4F, 0.0F, 0.0F, 0.5F});
        CHECK(result.r == doctest::Approx(0.4F).epsilon(0.01));
    }

    SUBCASE("Add accumulates past what one layer could reach") {
        const Color result =
            drawOver(Color{0.25F, 0.0F, 0.0F, 1.0F}, BlendMode::Add, Color{0.5F, 0.0F, 0.0F, 1.0F});
        CHECK(result.r == doctest::Approx(0.75F).epsilon(0.01));
    }

    SUBCASE("AddAlpha weights the addition by source alpha") {
        const Color result = drawOver(
            Color{0.25F, 0.0F, 0.0F, 1.0F}, BlendMode::AddAlpha, Color{0.5F, 0.0F, 0.0F, 0.5F});
        CHECK(result.r == doctest::Approx(0.5F).epsilon(0.01));
    }

    SUBCASE("Screen approaches white instead of clipping") {
        const Color result = drawOver(
            Color{0.5F, 0.0F, 0.0F, 1.0F}, BlendMode::Screen, Color{0.5F, 0.0F, 0.0F, 1.0F});
        // 0.5 + 0.5 * (1 - 0.5)
        CHECK(result.r == doctest::Approx(0.75F).epsilon(0.01));
        CHECK(result.r < 1.0F);
    }

    SUBCASE("Over composites colour and leaves target alpha alone") {
        const Color result = drawOver(
            Color{0.0F, 0.0F, 0.0F, 0.25F}, BlendMode::Over, Color{1.0F, 0.0F, 0.0F, 0.5F});
        CHECK(result.r == doctest::Approx(0.5F).epsilon(0.01));
        CHECK(result.a == doctest::Approx(0.25F).epsilon(0.01));
    }

    device.bindFramebuffer(FramebufferHandle{});
    CHECK(device.takeErrors().empty());

    device.destroyFramebuffer(target.value());
    device.destroyBuffer(quad.vertices);
    device.destroyShader(shader.value());
}

TEST_CASE("a target above 8 bits per channel holds values an RGBA8 one would clip") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto target = device.createFramebuffer(
        FramebufferDesc{.width = 4, .height = 4, .format = TextureFormat::RGBA16F});
    REQUIRE(target);

    device.bindFramebuffer(target.value());
    device.clear(Color{4.0F, 0.0F, 0.0F, 1.0F});

    // Headroom above 1.0 is the whole reason compositing happens in RGBA16F:
    // an 8-bit target would have clamped this to white and lost the glow.
    CHECK(firstPixel(device, target.value(), 4, 4).r == doctest::Approx(4.0F).epsilon(0.01));

    device.bindFramebuffer(FramebufferHandle{});
    device.destroyFramebuffer(target.value());
}

TEST_CASE("steady-state drawing allocates nothing on the GPU") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    auto shader = device.createShader(
        build("quad.vert", kVertexSource), build("quad.frag", kFragmentSource), "quad");
    REQUIRE(shader);
    const Quad quad = makeQuad(device);

    auto target = device.createFramebuffer(FramebufferDesc{.width = 16, .height = 16});
    REQUIRE(target);

    const DrawCall call{.shader = shader.value(),
                        .vertices = quad.vertices,
                        .layout = &quad.layout,
                        .primitive = Primitive::TriangleStrip,
                        .count = 4,
                        .instanceCount = 1};

    // First frame builds the vertex array object.
    device.beginFrame();
    device.bindFramebuffer(target.value());
    device.draw(call);
    CHECK(device.stats().drawCalls == 1);

    // Every frame after it reuses everything.
    for (int frame = 0; frame < 3; ++frame) {
        device.beginFrame();
        device.clear(Color{0.0F, 0.0F, 0.0F, 1.0F});
        device.draw(call);
        device.draw(call);

        CHECK(device.stats().drawCalls == 2);
        CHECK(device.stats().gpuAllocations == 0);
        CHECK(device.stats().bytesUploaded == 0);
    }

    device.bindFramebuffer(FramebufferHandle{});
    device.destroyFramebuffer(target.value());
    device.destroyBuffer(quad.vertices);
    device.destroyShader(shader.value());
}

TEST_CASE("a chain of effects aliases down to two framebuffers") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    DeviceTargetAllocator allocator(device);
    RenderTargetPool pool(allocator);

    const auto buildChain = [](FrameGraph& graph) {
        PassId previous = graph.addPass(PassDesc{.name = "source", .width = 32, .height = 32});
        for (int effect = 0; effect < 5; ++effect) {
            PassDesc pass{.name = "effect", .width = 32, .height = 32};
            pass.inputs = {previous};
            previous = graph.addPass(std::move(pass));
        }

        PassDesc screen{.name = "screen", .width = 32, .height = 32};
        screen.inputs = {previous};
        screen.present = true;
        graph.addPass(std::move(screen));
    };

    for (int frame = 0; frame < 3; ++frame) {
        FrameGraph graph;
        buildChain(graph);
        REQUIRE(graph.compile());
        graph.execute(device, pool);
        pool.endFrame();
    }

    // Six passes, but each target is free the moment its single reader is
    // done, so two framebuffers serve the whole chain — and the second and
    // third frames allocate nothing at all.
    CHECK(pool.allocations() == 2);
    CHECK(pool.inUse() == 0);
    CHECK(device.takeErrors().empty());
}

TEST_CASE("a pass reads the texture an input pass rendered") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    DeviceTargetAllocator allocator(device);
    RenderTargetPool pool(allocator);

    FrameGraph graph;
    TextureHandle seen;
    TextureHandle undeclared;

    const PassId source = graph.addPass(PassDesc{.name = "source", .width = 16, .height = 16});
    const PassId other = graph.addPass(PassDesc{.name = "other", .width = 16, .height = 16});

    PassDesc reader{.name = "reader", .width = 16, .height = 16};
    reader.inputs = {source};
    reader.execute = [&](const PassContext& context) {
        seen = context.input(source);
        // Reading a pass that was not declared as an input gives nothing,
        // rather than a target the pool may already have recycled.
        undeclared = context.input(other);
    };
    graph.addPass(std::move(reader));

    REQUIRE(graph.compile());
    graph.execute(device, pool);

    CHECK(seen.valid());
    CHECK_FALSE(undeclared.valid());
    CHECK(device.textureWidth(seen) == 16);
}

TEST_CASE("redundant state changes are not sent to the driver") {
    SPECTRA_REQUIRE_GL();
    Device& device = *gl().device;

    device.beginFrame();
    device.setBlendMode(BlendMode::Add);
    const std::uint32_t afterFirst = device.stats().stateChanges;

    device.setBlendMode(BlendMode::Add);
    device.setBlendMode(BlendMode::Add);

    CHECK(device.stats().stateChanges == afterFirst);
}
