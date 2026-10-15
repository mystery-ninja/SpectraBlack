#pragma once

#include <spectra/core/node.hpp>
#include <spectra/core/registry.hpp>
#include <spectra/core/signal.hpp>
#include <spectra/core/types.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spectra {

/// A scene flattened into a form that can be evaluated without walking the
/// tree.
///
/// Evaluating by recursing through nodes and dispatching per property is how a
/// 0.3 ms frame becomes a 4 ms one: pointer chasing, cache misses and a branch
/// on node type for every value. Instead the topology is resolved once into a
/// flat op list, and each frame walks that list and writes into one contiguous
/// output buffer. Renderers then read results by slot index and never touch
/// the node tree, which is also what lets rendering run off the UI thread.
///
/// **The plan borrows from the scene.** It holds pointers into the nodes it
/// was built from, so it is invalidated by anything that adds, removes, or
/// reorders nodes or properties, and by the scene being destroyed. Rebuild it
/// after any structural edit; it is cheap. Property *values* and keyframes may
/// change freely without a rebuild.
class EvalPlan {
public:
    /// Where one property's evaluated components live in the output buffer.
    struct Slot {
        std::uint32_t offset{0};
        std::uint32_t count{0};
    };

    /// Build from a node and everything under it.
    void build(Node& root);

    void clear();

    /// Evaluate every property at `t`. `signals` may be null, in which case
    /// drivers are skipped and only keyframe curves apply.
    void evaluate(Seconds t, const SignalProvider* signals = nullptr);

    [[nodiscard]] std::span<const float> output() const { return outputs_; }

    [[nodiscard]] std::size_t propertyCount() const { return ops_.size(); }

    /// Slot for a node's property, or nothing if either is unknown.
    [[nodiscard]] std::optional<Slot> slotOf(std::string_view nodeId,
                                             std::string_view propertyName) const;

    /// Convenience readers. These return zero when the slot does not exist,
    /// which keeps a renderer missing one property from taking the frame down.
    [[nodiscard]] float value(std::string_view nodeId, std::string_view propertyName) const;
    [[nodiscard]] Vec2 vec2(std::string_view nodeId, std::string_view propertyName) const;
    [[nodiscard]] Color color(std::string_view nodeId, std::string_view propertyName) const;

    /// Nodes in the order they were flattened: parents before children, and
    /// siblings in tree order. Renderers rely on this being draw order.
    [[nodiscard]] std::span<Node* const> nodes() const { return nodes_; }

private:
    struct Op {
        const void* target{nullptr};
        PropertyKind kind{PropertyKind::Float};
        std::uint32_t offset{0};
    };

    void addNode(Node& node);

    std::vector<Op> ops_;
    std::vector<float> outputs_;
    std::vector<Node*> nodes_;

    /// Keyed by "<nodeId>\0<propertyName>" so one lookup serves both parts.
    std::unordered_map<std::string, Slot> index_;
};

}  // namespace spectra
