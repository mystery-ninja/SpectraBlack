#pragma once

#include <spectra/core/result.hpp>
#include <spectra/gpu/device.hpp>
#include <spectra/gpu/target_pool.hpp>
#include <spectra/gpu/types.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace spectra::gpu {

using PassId = std::uint32_t;

class FrameGraph;

/// What a pass is handed when it runs.
struct PassContext {
    Device& device;
    FramebufferHandle target;  ///< Null for the pass that writes to the screen.
    int width{0};
    int height{0};

    /// The texture an input pass produced. Null if `pass` is not one of this
    /// pass's declared inputs, which keeps an undeclared read from quietly
    /// working until the day the pool recycles that target.
    [[nodiscard]] TextureHandle input(PassId pass) const;

    const FrameGraph* graph{nullptr};
    const PassId* inputs{nullptr};
    std::size_t inputCount{0};
};

struct PassDesc {
    std::string name;

    int width{0};
    int height{0};
    TextureFormat format{TextureFormat::RGBA16F};
    bool depth{false};

    /// Passes whose results this one samples. The graph orders execution from
    /// these, and uses them to decide when a target can go back to the pool.
    std::vector<PassId> inputs;

    /// Writes to the window or the export target rather than to a pooled
    /// framebuffer. Exactly one pass should be marked this way.
    bool present{false};

    std::function<void(const PassContext&)> execute;
};

/// Per-frame pass list with explicit dependencies.
///
/// Resolving order and target lifetimes here, rather than having renderers
/// bind framebuffers themselves, is what lets targets be aliased: six effects
/// in a chain use two framebuffers, not six. See docs/03-RENDER-ENGINE.md.
///
/// Rebuilt every frame. It holds no GPU resources of its own and compiling it
/// is a topological sort over a handful of nodes, which is why the frame
/// budget for this is 0.1 ms.
class FrameGraph {
public:
    /// The id is a convenience for wiring up a later pass's inputs; a pass
    /// nothing reads is free to ignore it.
    PassId addPass(PassDesc pass);

    void clear();

    [[nodiscard]] std::size_t passCount() const { return passes_.size(); }

    [[nodiscard]] const PassDesc& pass(PassId id) const { return passes_[id]; }

    /// Resolve execution order. Fails on a cycle or an input that does not
    /// exist, naming the passes involved rather than hanging or rendering
    /// something subtly wrong.
    [[nodiscard]] Result<std::vector<PassId>> compile();

    /// Valid after a successful `compile`.
    [[nodiscard]] std::span<const PassId> order() const { return order_; }

    /// The last pass in execution order that reads `pass`, or `pass` itself
    /// when nothing does. Its target returns to the pool once that reader has
    /// finished.
    [[nodiscard]] PassId lastReaderOf(PassId pass) const;

    /// Run the passes. A `present` pass renders into `presentTarget`, which
    /// is null for the window in preview and the readback framebuffer in
    /// export — the only difference between the two paths.
    void execute(Device& device, RenderTargetPool& pool, FramebufferHandle presentTarget = {});

    /// Target a pass rendered into, valid only while the pass's readers are
    /// still pending. Used by `PassContext::input`.
    [[nodiscard]] FramebufferHandle targetOf(PassId pass) const;

private:
    std::vector<PassDesc> passes_;
    std::vector<PassId> order_;
    std::vector<PassId> lastReader_;
    std::vector<FramebufferHandle> targets_;
};

}  // namespace spectra::gpu
