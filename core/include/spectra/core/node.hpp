#pragma once

#include <spectra/core/property.hpp>
#include <spectra/core/registry.hpp>
#include <spectra/core/types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spectra {

/// Opaque holder for JSON a node did not understand. Declared here and defined
/// in `json_extras.hpp` so that the node headers — which every module includes
/// — do not pull in the JSON library.
class JsonExtras;

/// Node type tag.
///
/// An explicit tag rather than RTTI: `dynamic_cast` is unavailable to us by
/// house rule (docs/02-TECH-STACK.md), and a stable enum is also what the save
/// format and the node factory key off.
enum class NodeType : std::uint16_t {
    Unknown,  ///< A type this build does not know. Preserved, never discarded.
    Group,
    Composition,
    SolidColor,
    Image,
    Text,
    SpectrumBars,
    AudioSource,
};

[[nodiscard]] std::string_view nodeTypeName(NodeType type);
[[nodiscard]] NodeType nodeTypeFromName(std::string_view name);

/// How a node composites onto what is beneath it. Avee's set, which covers
/// what a visualizer needs. See docs/09-AVEE-TEARDOWN.md.
enum class BlendMode : std::uint8_t {
    Alpha,
    PreMulAlpha,
    Add,
    AddAlpha,
    Screen,
    Over,
};

[[nodiscard]] std::string_view blendModeName(BlendMode mode);
[[nodiscard]] BlendMode blendModeFromName(std::string_view name);

/// Position, scale, rotation, anchor and opacity, all animatable.
///
/// Spatial values are normalized against the smaller screen dimension, so a
/// scene authored at 1080p is correct at 4K and on a phone. This is Avee's
/// `Meter` convention and we adopt it wholesale.
struct Transform2D {
    Property<Vec2> position{Vec2{0.5f, 0.5f}};
    Property<Vec2> scale{Vec2{1.0f, 1.0f}};
    Property<float> rotation{0.0f};
    Property<Vec2> anchor{Vec2{0.5f, 0.5f}};
    Property<float> opacity{1.0f};

    void describe(PropertyRegistry& r);
};

/// Base of every element in the scene.
///
/// Effects are nodes too, rather than a separate per-layer effect stack. That
/// is Avee's structure and it is the better one: one tree, one traversal, one
/// inspector, instead of two parallel systems for the same idea. After Effects
/// has both and pays for it.
class Node {
public:
    explicit Node(NodeType type, std::string id = {});
    virtual ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    [[nodiscard]] NodeType type() const { return type_; }

    [[nodiscard]] const std::string& id() const { return id_; }

    void setId(std::string id) { id_ = std::move(id); }

    std::string name;
    bool enabled{true};
    BlendMode blendMode{BlendMode::Alpha};
    Transform2D transform;

    /// Declare this node's parameters. Overrides must call the base first, so
    /// that Transform comes out at a consistent place in every inspector.
    virtual void describe(PropertyRegistry& r);

    /// Whether this node may contain others.
    [[nodiscard]] virtual bool acceptsChildren() const { return false; }

    [[nodiscard]] const std::vector<std::unique_ptr<Node>>& children() const { return children_; }

    /// Append a child. Returns a borrowed pointer, or null if this node does
    /// not accept children or the child is null.
    Node* addChild(std::unique_ptr<Node> child);

    /// Detach a child and hand back ownership. Null if the index is out of
    /// range. This is what undo holds on to after a delete.
    std::unique_ptr<Node> removeChild(std::size_t index);

    /// Insert at a position, clamped to the end.
    Node* insertChild(std::size_t index, std::unique_ptr<Node> child);

    [[nodiscard]] Node* find(std::string_view id);
    [[nodiscard]] const Node* find(std::string_view id) const;

    /// Keys from a file written by a newer build. Carried through a load/save
    /// round trip untouched so that opening someone's project in an older
    /// version does not quietly delete their work.
    std::shared_ptr<JsonExtras> extras;

    /// Collect this node's descriptors. Convenience around `describe`.
    [[nodiscard]] PropertyRegistry describeProperties();

private:
    NodeType type_;
    std::string id_;
    std::vector<std::unique_ptr<Node>> children_;
};

/// A plain container. Transform applies to everything inside it.
class GroupNode final : public Node {
public:
    explicit GroupNode(std::string id = {}) : Node(NodeType::Group, std::move(id)) {}

    [[nodiscard]] bool acceptsChildren() const override { return true; }
};

/// A container that renders to its own buffer before compositing.
class CompositionNode final : public Node {
public:
    explicit CompositionNode(std::string id = {});

    [[nodiscard]] bool acceptsChildren() const override { return true; }

    void describe(PropertyRegistry& r) override;

    Property<Color> background{Color{0.0f, 0.0f, 0.0f, 0.0f}};
    Property<float> introFade{0.0f};
    Property<float> outroFade{0.0f};
};

class SolidColorNode final : public Node {
public:
    explicit SolidColorNode(std::string id = {}) : Node(NodeType::SolidColor, std::move(id)) {}

    void describe(PropertyRegistry& r) override;

    Property<Color> color{Color{1.0f, 1.0f, 1.0f, 1.0f}};
    Property<Vec2> size{Vec2{1.0f, 1.0f}};
};

class ImageNode final : public Node {
public:
    explicit ImageNode(std::string id = {}) : Node(NodeType::Image, std::move(id)) {}

    void describe(PropertyRegistry& r) override;

    /// Relative path from the project file. Absolute paths are rejected on
    /// load because they break the moment a project is shared.
    std::string source;
    Property<Color> tint{Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

class TextNode final : public Node {
public:
    explicit TextNode(std::string id = {}) : Node(NodeType::Text, std::move(id)) {}

    void describe(PropertyRegistry& r) override;

    /// Supports `{Artist}`, `{Title}` and time fields, which is Avee's
    /// `StatText` folded into Text rather than kept as a separate type.
    std::string text;
    std::string fontFamily;
    Property<float> size{0.08f};
    Property<Color> color{Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

class SpectrumBarsNode final : public Node {
public:
    explicit SpectrumBarsNode(std::string id = {}) : Node(NodeType::SpectrumBars, std::move(id)) {}

    void describe(PropertyRegistry& r) override;

    Property<int> barCount{64};
    Property<float> barWidth{0.6f};
    Property<float> barSpacing{0.4f};
    Property<float> smoothing{0.5f};
    Property<int> scaleMode{0};  ///< 0 linear, 1 log, 2 mel
    Property<Color> colorA{Color{1.0f, 1.0f, 1.0f, 1.0f}};
    Property<Color> colorB{Color{0.3f, 0.6f, 1.0f, 1.0f}};

    static std::span<const std::string_view> scaleModeOptions();
};

/// Binds a scene, or a subtree of it, to an audio signal source.
///
/// Avee resolves this by taking the *last* `AudioProvider` in the layer list,
/// which is order-dependent and surprising. Ours is referenced explicitly by
/// id, the same reasoning as masks. See docs/14-VIZ-COMPATIBILITY.md.
class AudioSourceNode final : public Node {
public:
    explicit AudioSourceNode(std::string id = {}) : Node(NodeType::AudioSource, std::move(id)) {}

    void describe(PropertyRegistry& r) override;

    std::string source;
    Property<float> gain{1.0f};
};

/// Stands in for a node type this build does not recognise.
///
/// It renders as nothing and keeps every byte of its original JSON, so a
/// project saved by a newer version survives a round trip through an older
/// one. Avee returns the string `"unk"` here and loses the data outright.
class UnknownNode final : public Node {
public:
    explicit UnknownNode(std::string id = {}) : Node(NodeType::Unknown, std::move(id)) {}

    [[nodiscard]] bool acceptsChildren() const override { return true; }

    /// The `type` string as it appeared in the file, so re-saving writes back
    /// what was read rather than the word "Unknown".
    std::string originalType;
};

/// Construct a node of the given type. Null for `NodeType::Unknown`, which has
/// to be built by the loader because it needs the original type name.
[[nodiscard]] std::unique_ptr<Node> makeNode(NodeType type, std::string id = {});

}  // namespace spectra
