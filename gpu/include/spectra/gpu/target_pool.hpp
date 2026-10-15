#pragma once

#include <spectra/core/result.hpp>
#include <spectra/gpu/device.hpp>
#include <spectra/gpu/handles.hpp>
#include <spectra/gpu/types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spectra::gpu {

/// What makes two render targets interchangeable.
struct TargetKey {
    int width{0};
    int height{0};
    TextureFormat format{TextureFormat::RGBA16F};
    bool depth{false};

    friend bool operator==(const TargetKey& lhs, const TargetKey& rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height && lhs.format == rhs.format &&
               lhs.depth == rhs.depth;
    }
};

/// Where the pool gets its framebuffers.
///
/// An interface so the pool's reuse and aging rules can be tested without a
/// GPU. Those rules are the part that goes wrong; the GL calls behind them
/// are three lines.
class TargetAllocator {
public:
    virtual ~TargetAllocator() = default;

    TargetAllocator() = default;
    TargetAllocator(const TargetAllocator&) = default;
    TargetAllocator(TargetAllocator&&) = default;
    TargetAllocator& operator=(const TargetAllocator&) = default;
    TargetAllocator& operator=(TargetAllocator&&) = default;

    [[nodiscard]] virtual Result<FramebufferHandle> createTarget(const TargetKey& key) = 0;
    virtual void destroyTarget(FramebufferHandle target) = 0;
};

/// The real allocator, backed by a device.
class DeviceTargetAllocator final : public TargetAllocator {
public:
    explicit DeviceTargetAllocator(Device& device) : device_(&device) {}

    [[nodiscard]] Result<FramebufferHandle> createTarget(const TargetKey& key) override;
    void destroyTarget(FramebufferHandle target) override;

private:
    Device* device_;
};

/// Recycles render targets across passes and across frames.
///
/// Avee gives every non-final composition its own framebuffer and holds it
/// forever, so framebuffer memory scales with composition count — at 4K that
/// is 32 MB each. Here a target is borrowed for as long as something still
/// reads it and returned immediately after, so memory tracks the *depth* of
/// the frame graph rather than the size of the scene.
///
/// Targets idle for `framesUntilRelease` frames are freed, which is what
/// gives the VRAM back after a user deletes a heavy effect. Too eager and a
/// resize thrashes; 300 frames is about five seconds, the value
/// docs/03-RENDER-ENGINE.md settles on.
class RenderTargetPool {
public:
    explicit RenderTargetPool(TargetAllocator& allocator, int framesUntilRelease = 300);

    ~RenderTargetPool();

    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;
    RenderTargetPool(RenderTargetPool&&) = delete;
    RenderTargetPool& operator=(RenderTargetPool&&) = delete;

    /// Borrow a target. Reuses a matching free one when there is one, which
    /// in steady state is always.
    [[nodiscard]] FramebufferHandle acquire(const TargetKey& key);

    /// Hand it back. Releasing a handle the pool did not give out is ignored.
    void release(FramebufferHandle target);

    /// Ages the free list and frees what has been idle too long. Call once
    /// per frame, after the graph has executed.
    void endFrame();

    /// Frees everything, including targets still checked out. For a device
    /// teardown or a resolution change.
    void clear();

    [[nodiscard]] std::size_t inUse() const { return live_.size(); }

    [[nodiscard]] std::size_t available() const { return free_.size(); }

    /// Cumulative targets created. Stops growing once the pool is warm, which
    /// is the property worth asserting in a test.
    [[nodiscard]] std::size_t allocations() const { return allocations_; }

    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    struct Entry {
        TargetKey key;
        FramebufferHandle target;
        std::uint64_t idleSince{0};
    };

    TargetAllocator* allocator_;
    std::vector<Entry> free_;
    std::vector<Entry> live_;
    std::uint64_t frame_{0};
    int framesUntilRelease_;
    std::size_t allocations_{0};
    std::string lastError_;
};

}  // namespace spectra::gpu
