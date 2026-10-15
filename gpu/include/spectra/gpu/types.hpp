#pragma once

#include <spectra/gpu/handles.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::gpu {

/// Pixel formats, restricted to what GL 4.3 and GLES 3.2 both guarantee as
/// colour-renderable.
///
/// `RGBA16F` is the working format for everything offscreen: compositing
/// happens in linear light, and 8 bits per channel bands visibly once two
/// additive glows overlap. Avee composites in sRGB8 and it shows. See
/// docs/03-RENDER-ENGINE.md.
enum class TextureFormat : std::uint8_t {
    RGBA8,    ///< Final output, and source images after decode.
    RGBA16F,  ///< Linear-light working format. The default for render targets.
    R8,       ///< Masks and single-channel data.
    R16F,     ///< Spectrum uploads, where 8 bits is not enough dynamic range.
    Depth24,
};

[[nodiscard]] std::uint32_t bytesPerPixel(TextureFormat format);

[[nodiscard]] bool isDepthFormat(TextureFormat format);

enum class TextureFilter : std::uint8_t {
    Nearest,
    Linear,
};

enum class TextureWrap : std::uint8_t {
    ClampToEdge,
    Repeat,
    MirroredRepeat,
};

struct TextureDesc {
    int width{0};
    int height{0};
    TextureFormat format{TextureFormat::RGBA16F};
    TextureFilter filter{TextureFilter::Linear};
    TextureWrap wrap{TextureWrap::ClampToEdge};
    bool mipmaps{false};

    /// Optional initial contents, tightly packed, matching `format`.
    const void* pixels{nullptr};

    std::string debugName;
};

enum class BufferType : std::uint8_t {
    Vertex,
    Index,
    Uniform,
};

/// How often the contents change, which decides the storage strategy.
///
/// `Dynamic` buffers are persistently mapped where the driver supports it, so
/// per-frame geometry costs a memcpy into already-mapped memory instead of a
/// driver-side allocation. The 1.0 ms geometry budget in
/// docs/03-RENDER-ENGINE.md assumes this.
enum class BufferUpdate : std::uint8_t {
    Static,
    Dynamic,
};

struct BufferDesc {
    BufferType type{BufferType::Vertex};
    BufferUpdate update{BufferUpdate::Static};
    std::size_t size{0};
    const void* data{nullptr};
    std::string debugName;
};

enum class AttributeType : std::uint8_t {
    Float32,
    UInt8Normalized,
};

struct VertexAttribute {
    std::uint32_t location{0};
    std::uint32_t components{0};  ///< 1 to 4.
    AttributeType type{AttributeType::Float32};
    std::uint32_t offset{0};

    /// Advance once per instance rather than per vertex. All the bars in a
    /// spectrum come from one instanced draw, which is the fix for the
    /// per-bar draw call problem called out in docs/03-RENDER-ENGINE.md.
    bool perInstance{false};
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    std::uint32_t stride{0};
};

enum class Primitive : std::uint8_t {
    Triangles,
    TriangleStrip,
    Lines,
};

enum class IndexType : std::uint8_t {
    None,
    UInt16,
    UInt32,
};

struct DrawCall {
    ShaderHandle shader;
    BufferHandle vertices;
    BufferHandle indices;  ///< Null for a non-indexed draw.
    const VertexLayout* layout{nullptr};
    Primitive primitive{Primitive::Triangles};
    IndexType indexType{IndexType::None};

    std::uint32_t first{0};          ///< First vertex, or first index.
    std::uint32_t count{0};          ///< Vertices, or indices.
    std::uint32_t instanceCount{1};  ///< Zero draws nothing.
};

/// How a draw composites onto the target.
///
/// Avee's six, which turns out to be most of what a visualizer needs. The
/// Photoshop set needs shader-based blending and arrives in Phase 2. See
/// docs/03-RENDER-ENGINE.md.
enum class BlendMode : std::uint8_t {
    None,         ///< Overwrite. The right choice for a full-screen base layer.
    Alpha,        ///< Straight alpha over.
    PreMulAlpha,  ///< Premultiplied, correct for filtered textures.
    Add,          ///< Glows, light, energy.
    AddAlpha,     ///< Additive weighted by source alpha.
    Screen,       ///< Softer additive that cannot blow out.
    Over,         ///< Alpha-preserving: composites colour, leaves target alpha alone.
};

[[nodiscard]] std::string_view blendModeName(BlendMode mode);

enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    SrcColor,
    OneMinusSrcColor,
    DstAlpha,
    OneMinusDstAlpha,
};

/// The blend equation a mode resolves to.
///
/// Separated from the GL call so the table is a pure function, and so a
/// wrong factor is caught by a unit test rather than by noticing that glows
/// look wrong.
struct BlendState {
    bool enabled{true};
    BlendFactor colorSrc{BlendFactor::One};
    BlendFactor colorDst{BlendFactor::Zero};
    BlendFactor alphaSrc{BlendFactor::One};
    BlendFactor alphaDst{BlendFactor::Zero};

    friend bool operator==(const BlendState& lhs, const BlendState& rhs) {
        return lhs.enabled == rhs.enabled && lhs.colorSrc == rhs.colorSrc &&
               lhs.colorDst == rhs.colorDst && lhs.alphaSrc == rhs.alphaSrc &&
               lhs.alphaDst == rhs.alphaDst;
    }
};

[[nodiscard]] BlendState blendStateFor(BlendMode mode);

struct FramebufferDesc {
    int width{0};
    int height{0};
    TextureFormat format{TextureFormat::RGBA16F};
    TextureFilter filter{TextureFilter::Linear};
    bool depth{false};
    std::string debugName;
};

struct Viewport {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    friend bool operator==(const Viewport& lhs, const Viewport& rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
               lhs.height == rhs.height;
    }
};

/// Per-frame counters, for the frame time and allocation readout the editor
/// shows. `gpuAllocations` must be zero in steady state — that is the Stage 2
/// exit criterion, and the only way to notice it regressing is to measure it.
struct FrameStats {
    std::uint32_t drawCalls{0};
    std::uint32_t stateChanges{0};
    std::uint32_t gpuAllocations{0};
    std::size_t bytesUploaded{0};
};

}  // namespace spectra::gpu
