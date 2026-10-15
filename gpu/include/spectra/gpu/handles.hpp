#pragma once

#include <cstdint>
#include <functional>

namespace spectra::gpu {

/// Resource references are opaque integers rather than pointers, which makes
/// them trivially copyable, safe to put in a command list that outlives the
/// call that built it, and usable as keys in the render target pool. See
/// docs/03-RENDER-ENGINE.md.
///
/// The integer is not the GL name. It packs a slot index with a generation
/// counter, so a handle to a destroyed resource is detected rather than
/// silently addressing whatever was created in its place — the kind of bug
/// that otherwise shows up as one wrong texture, once, on someone else's
/// machine. Zero is the null handle and never refers to anything.
struct TextureHandle {
    std::uint32_t id{0};

    [[nodiscard]] bool valid() const { return id != 0; }

    explicit operator bool() const { return valid(); }

    friend bool operator==(TextureHandle lhs, TextureHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(TextureHandle lhs, TextureHandle rhs) { return lhs.id != rhs.id; }
};

struct BufferHandle {
    std::uint32_t id{0};

    [[nodiscard]] bool valid() const { return id != 0; }

    explicit operator bool() const { return valid(); }

    friend bool operator==(BufferHandle lhs, BufferHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(BufferHandle lhs, BufferHandle rhs) { return lhs.id != rhs.id; }
};

struct ShaderHandle {
    std::uint32_t id{0};

    [[nodiscard]] bool valid() const { return id != 0; }

    explicit operator bool() const { return valid(); }

    friend bool operator==(ShaderHandle lhs, ShaderHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(ShaderHandle lhs, ShaderHandle rhs) { return lhs.id != rhs.id; }
};

struct FramebufferHandle {
    std::uint32_t id{0};

    [[nodiscard]] bool valid() const { return id != 0; }

    explicit operator bool() const { return valid(); }

    friend bool operator==(FramebufferHandle lhs, FramebufferHandle rhs) {
        return lhs.id == rhs.id;
    }

    friend bool operator!=(FramebufferHandle lhs, FramebufferHandle rhs) {
        return lhs.id != rhs.id;
    }
};

/// Index and generation packing, shared by the backend and the tests that
/// check stale-handle detection.
namespace detail {

inline constexpr std::uint32_t kHandleIndexBits = 24;
inline constexpr std::uint32_t kHandleIndexMask = (1u << kHandleIndexBits) - 1u;

/// Slot 0 with generation 0 would encode as the null handle, so generations
/// start at 1 and wrap back to 1 rather than through 0.
[[nodiscard]] constexpr std::uint32_t packHandle(std::uint32_t index, std::uint32_t generation) {
    return (generation << kHandleIndexBits) | (index & kHandleIndexMask);
}

[[nodiscard]] constexpr std::uint32_t handleIndex(std::uint32_t id) {
    return id & kHandleIndexMask;
}

[[nodiscard]] constexpr std::uint32_t handleGeneration(std::uint32_t id) {
    return id >> kHandleIndexBits;
}

[[nodiscard]] constexpr std::uint32_t nextGeneration(std::uint32_t generation) {
    const std::uint32_t next = (generation + 1) & 0xFFu;
    return next == 0 ? 1u : next;
}

}  // namespace detail

}  // namespace spectra::gpu

template<>
struct std::hash<spectra::gpu::TextureHandle> {
    std::size_t operator()(spectra::gpu::TextureHandle handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id);
    }
};

template<>
struct std::hash<spectra::gpu::BufferHandle> {
    std::size_t operator()(spectra::gpu::BufferHandle handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id);
    }
};

template<>
struct std::hash<spectra::gpu::ShaderHandle> {
    std::size_t operator()(spectra::gpu::ShaderHandle handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id);
    }
};

template<>
struct std::hash<spectra::gpu::FramebufferHandle> {
    std::size_t operator()(spectra::gpu::FramebufferHandle handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id);
    }
};
