#include <spectra/gpu/quad_renderer.hpp>
#include <spectra/gpu/shader_source.hpp>

#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace spectra::gpu {
namespace {

/// The unit quad every draw is a transform of. Four corners as a strip, so
/// there is no index buffer and no per-quad geometry upload.
constexpr std::array<float, 8> kCorners{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};

constexpr std::string_view kQuadVertex = R"(
layout(location = 0) in vec2 aCorner;

uniform mat4 uTransform;

out vec2 vUv;

void main() {
    vUv = aCorner;
    gl_Position = uTransform * vec4(aCorner, 0.0, 1.0);
}
)";

constexpr std::string_view kQuadFragment = R"(
in vec2 vUv;

uniform sampler2D uTexture;
uniform vec4      uColor;
uniform int       uTextured;

out vec4 fragColor;

void main() {
    vec4 color = uColor;
    if (uTextured != 0) {
        color *= texture(uTexture, vUv);
    }
    fragColor = color;
}
)";

/// The final conversion out of linear light.
///
/// The piecewise sRGB transfer function, not a 2.2 gamma approximation: the
/// two differ most in the darks, which is exactly where banding shows up.
constexpr std::string_view kPresentFragment = R"(
in vec2 vUv;

uniform sampler2D uTexture;

out vec4 fragColor;

vec3 linearToSrgb(vec3 linear) {
    vec3 clamped = clamp(linear, 0.0, 1.0);
    vec3 low     = clamped * 12.92;
    vec3 high    = 1.055 * pow(clamped, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), clamped));
}

void main() {
    vec4 color = texture(uTexture, vUv);
    fragColor  = vec4(linearToSrgb(color.rgb), color.a);
}
)";

[[nodiscard]] Result<ShaderHandle> buildShader(Device& device,
                                               std::string_view name,
                                               std::string_view vertex,
                                               std::string_view fragment) {
    const auto vertexSource = preprocessShader(std::string(name) + ".vert", vertex);
    if (!vertexSource) {
        return Result<ShaderHandle>::error(vertexSource.error());
    }
    const auto fragmentSource = preprocessShader(std::string(name) + ".frag", fragment);
    if (!fragmentSource) {
        return Result<ShaderHandle>::error(fragmentSource.error());
    }

    auto shader =
        device.createShader(vertexSource.value().source, fragmentSource.value().source, name);
    if (!shader) {
        // Driver logs refer to source strings by index; the preprocessor
        // knows which file each one was.
        return Result<ShaderHandle>::error(fragmentSource.value().translateLog(shader.error()));
    }
    return shader;
}

}  // namespace

struct QuadRenderer::Impl {
    Device* device{nullptr};
    ShaderHandle quadShader;
    ShaderHandle presentShader;
    BufferHandle corners;
    VertexLayout layout;

    int targetWidth{0};
    int targetHeight{0};

    /// Places a quad in clip space.
    ///
    /// Built on the CPU because it is six multiplies per quad and uploading
    /// it as one matrix is cheaper than feeding the vertex shader the five
    /// values it would otherwise need.
    [[nodiscard]] Mat4 transformFor(const Quad& quad) const {
        const auto width = static_cast<float>(targetWidth);
        const auto height = static_cast<float>(targetHeight);
        if (width <= 0.0F || height <= 0.0F) {
            return Mat4(1.0F);
        }

        // One unit is the smaller dimension, so a square stays square and a
        // scene keeps its proportions at any aspect ratio.
        const float meter = std::min(width, height);
        const Vec2 sizePixels = quad.size * meter;
        const Vec2 originPixel = Vec2{quad.position.x * width, quad.position.y * height};

        const float radians = quad.rotation * 3.14159265358979F / 180.0F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);

        // Corner (0..1) -> offset from the anchor, in pixels.
        const Vec2 scale = sizePixels;
        const Vec2 offset = -quad.anchor * sizePixels;

        Mat4 transform(1.0F);

        // Columns, because GLM is column-major: this composes
        // translate(origin) * rotate * translate(offset) * scale.
        const Vec2 columnX{scale.x * cosine, scale.x * sine};
        const Vec2 columnY{-scale.y * sine, scale.y * cosine};
        const Vec2 translation{originPixel.x + offset.x * cosine - offset.y * sine,
                               originPixel.y + offset.x * sine + offset.y * cosine};

        // Pixels to clip space, with y pointing down the screen the way every
        // UI coordinate system does.
        const auto toClipX = [width](float x) { return (x / width) * 2.0F; };
        const auto toClipY = [height](float y) { return -(y / height) * 2.0F; };

        transform[0] = Vec4{toClipX(columnX.x), toClipY(columnX.y), 0.0F, 0.0F};
        transform[1] = Vec4{toClipX(columnY.x), toClipY(columnY.y), 0.0F, 0.0F};
        transform[3] =
            Vec4{toClipX(translation.x) - 1.0F, toClipY(translation.y) + 1.0F, 0.0F, 1.0F};
        return transform;
    }

    void submit(const Quad& quad, ShaderHandle shader) {
        device->setUniform(shader, "uTransform", transformFor(quad));
        device->setUniform(shader, "uColor", Vec4(quad.color));
        device->setUniform(shader, "uTextured", quad.texture.valid() ? 1 : 0);
        if (quad.texture.valid()) {
            device->setTexture(shader, "uTexture", quad.texture, 0);
        }
        device->setBlendMode(quad.blend);

        device->draw(DrawCall{.shader = shader,
                              .vertices = corners,
                              .layout = &layout,
                              .primitive = Primitive::TriangleStrip,
                              .count = 4,
                              .instanceCount = 1});
    }
};

QuadRenderer::QuadRenderer() : impl_(std::make_unique<Impl>()) {}

QuadRenderer::~QuadRenderer() {
    if (impl_->device == nullptr) {
        return;
    }
    impl_->device->destroyBuffer(impl_->corners);
    impl_->device->destroyShader(impl_->quadShader);
    impl_->device->destroyShader(impl_->presentShader);
}

Result<std::unique_ptr<QuadRenderer>> QuadRenderer::create(Device& device) {
    using ResultType = Result<std::unique_ptr<QuadRenderer>>;

    auto quadShader = buildShader(device, "quad", kQuadVertex, kQuadFragment);
    if (!quadShader) {
        return ResultType::error(quadShader.error());
    }
    auto presentShader = buildShader(device, "present", kQuadVertex, kPresentFragment);
    if (!presentShader) {
        device.destroyShader(quadShader.value());
        return ResultType::error(presentShader.error());
    }

    auto corners = device.createBuffer(BufferDesc{.type = BufferType::Vertex,
                                                  .update = BufferUpdate::Static,
                                                  .size = sizeof(kCorners),
                                                  .data = kCorners.data(),
                                                  .debugName = "quad corners"});
    if (!corners) {
        device.destroyShader(quadShader.value());
        device.destroyShader(presentShader.value());
        return ResultType::error(corners.error());
    }

    std::unique_ptr<QuadRenderer> renderer(new QuadRenderer());
    renderer->impl_->device = &device;
    renderer->impl_->quadShader = quadShader.value();
    renderer->impl_->presentShader = presentShader.value();
    renderer->impl_->corners = corners.value();
    renderer->impl_->layout.stride = sizeof(float) * 2;
    renderer->impl_->layout.attributes = {VertexAttribute{
        .location = 0, .components = 2, .type = AttributeType::Float32, .offset = 0}};

    return ResultType::ok(std::move(renderer));
}

void QuadRenderer::begin(int width, int height) {
    impl_->targetWidth = width;
    impl_->targetHeight = height;
}

void QuadRenderer::draw(const Quad& quad) {
    impl_->submit(quad, impl_->quadShader);
}

void QuadRenderer::drawFullscreen(TextureHandle texture, BlendMode blend, const Color& tint) {
    const auto width = static_cast<float>(impl_->targetWidth);
    const auto height = static_cast<float>(impl_->targetHeight);
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    // Expressed in the same units as any other quad, so there is one code
    // path and one place for a coordinate mistake to hide.
    const float meter = std::min(width, height);

    Quad quad;
    quad.position = Vec2{0.5F, 0.5F};
    quad.size = Vec2{width / meter, height / meter};
    quad.color = tint;
    quad.texture = texture;
    quad.blend = blend;
    draw(quad);
}

void QuadRenderer::present(TextureHandle source, int width, int height) {
    begin(width, height);

    Quad quad;
    quad.position = Vec2{0.5F, 0.5F};
    quad.size = Vec2{static_cast<float>(width), static_cast<float>(height)} /
                std::min(static_cast<float>(width), static_cast<float>(height));
    quad.texture = source;
    quad.blend = BlendMode::None;

    impl_->device->setTexture(impl_->presentShader, "uTexture", source, 0);
    impl_->submit(quad, impl_->presentShader);
}

}  // namespace spectra::gpu
