#pragma once

#include <spectra/core/result.hpp>
#include <spectra/core/types.hpp>
#include <spectra/gpu/handles.hpp>
#include <spectra/gpu/types.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace spectra::gpu {

/// Returns the address of a GL entry point, or null. `Window` supplies GLFW's.
///
/// Taking a loader rather than calling GLFW ourselves is what lets the export
/// path run on a context this module did not create.
using ProcAddressLoader = void* (*)(const char* name);

struct DeviceInfo {
    std::string renderer;
    std::string vendor;
    std::string version;

    /// True when buffer storage can stay mapped across frames. Without it,
    /// dynamic buffers fall back to orphan-and-upload, which still works but
    /// costs a driver allocation per update.
    bool persistentMapping{false};

    int maxTextureSize{0};
};

/// The whole of our OpenGL surface area.
///
/// Thin by intent: we are not hiding GL's model, we are giving it a typed
/// surface so that no renderer can change global state behind another's back.
/// The state this object owns, and nothing else may touch: the bound
/// framebuffer, blend state, viewport, bound program, and texture units.
///
/// Not thread-safe, and deliberately so — it belongs to whichever thread owns
/// the GL context. Export gets its own context and its own Device.
class Device {
public:
    /// Loads the GL entry points and checks that the context meets our
    /// minimum. Fails with a readable message rather than crashing later on a
    /// null function pointer, which is what an unchecked loader gives you.
    [[nodiscard]] static Result<std::unique_ptr<Device>> create(ProcAddressLoader loader);

    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] const DeviceInfo& info() const;

    // --- Textures ----------------------------------------------------------

    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc);
    void destroyTexture(TextureHandle texture);

    /// Replace the whole image. `pixels` must be tightly packed and match the
    /// texture's format.
    void updateTexture(TextureHandle texture, const void* pixels);

    [[nodiscard]] int textureWidth(TextureHandle texture) const;
    [[nodiscard]] int textureHeight(TextureHandle texture) const;

    // --- Buffers -----------------------------------------------------------

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc);
    void destroyBuffer(BufferHandle buffer);

    /// Write into a buffer at `offset`. On a `Dynamic` buffer with persistent
    /// mapping this is a memcpy with no driver involvement.
    void updateBuffer(BufferHandle buffer, std::span<const std::byte> data, std::size_t offset = 0);

    [[nodiscard]] std::size_t bufferSize(BufferHandle buffer) const;

    // --- Shaders -----------------------------------------------------------

    /// Compile and link. Both sources must already have been through
    /// `preprocessShader`; the device does not add a `#version` line.
    ///
    /// On failure the message is the driver's log, with source-string indices
    /// translated back to file names when `files` is supplied.
    [[nodiscard]] Result<ShaderHandle> createShader(std::string_view vertexSource,
                                                    std::string_view fragmentSource,
                                                    std::string_view debugName = {});

    void destroyShader(ShaderHandle shader);

    /// Uniforms apply to `shader` whether or not it is the bound one, so a
    /// caller never has to think about bind order. Unknown names are ignored:
    /// a uniform the compiler removed as unused is not an error.
    void setUniform(ShaderHandle shader, std::string_view name, float value);
    void setUniform(ShaderHandle shader, std::string_view name, int value);
    void setUniform(ShaderHandle shader, std::string_view name, Vec2 value);
    void setUniform(ShaderHandle shader, std::string_view name, Vec3 value);
    void setUniform(ShaderHandle shader, std::string_view name, Vec4 value);
    void setUniform(ShaderHandle shader, std::string_view name, const Mat4& value);

    /// Bind a texture to a unit and point the sampler at it.
    void setTexture(ShaderHandle shader,
                    std::string_view name,
                    TextureHandle texture,
                    std::uint32_t unit = 0);

    // --- Framebuffers ------------------------------------------------------

    [[nodiscard]] Result<FramebufferHandle> createFramebuffer(const FramebufferDesc& desc);
    void destroyFramebuffer(FramebufferHandle framebuffer);

    /// The colour attachment, for use as a texture in a later pass.
    [[nodiscard]] TextureHandle framebufferTexture(FramebufferHandle framebuffer) const;

    [[nodiscard]] int framebufferWidth(FramebufferHandle framebuffer) const;
    [[nodiscard]] int framebufferHeight(FramebufferHandle framebuffer) const;

    // --- State and drawing -------------------------------------------------

    /// A null handle binds the window's framebuffer. Binding also sets the
    /// viewport to the target's full size, because forgetting that is the
    /// single most common cause of a pass rendering into a corner.
    void bindFramebuffer(FramebufferHandle framebuffer);

    void setViewport(const Viewport& viewport);
    void setBlendMode(BlendMode mode);

    /// Clears the bound framebuffer. The colour is linear, not sRGB.
    void clear(const Color& color);

    void draw(const DrawCall& call);

    /// Read a target's colour back as linear RGBA floats, four per pixel,
    /// bottom row first as GL stores it.
    ///
    /// Synchronous, so it stalls the pipeline — fine for the golden-image and
    /// parity tests it exists for. Export gets the double-buffered PBO path in
    /// Stage 6. Does nothing if `out` is not exactly large enough.
    void readPixels(FramebufferHandle framebuffer, std::span<float> out);

    /// Resets the per-frame counters. Call once at the top of a frame.
    void beginFrame();

    [[nodiscard]] const FrameStats& stats() const;

    /// Drains the GL error queue into a message, for use after a suspicious
    /// sequence. Empty when the queue was clean. Debug builds also install
    /// `GL_KHR_debug` output where the driver offers it, which reports more
    /// and reports it closer to the cause.
    [[nodiscard]] std::string takeErrors();

private:
    Device();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spectra::gpu
