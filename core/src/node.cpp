#include <spectra/core/node.hpp>

#include <array>
#include <utility>

namespace spectra {
namespace {

constexpr std::array<std::pair<NodeType, std::string_view>, 8> kNodeTypeNames{{
    {NodeType::Unknown, "Unknown"},
    {NodeType::Group, "Group"},
    {NodeType::Composition, "Composition"},
    {NodeType::SolidColor, "SolidColor"},
    {NodeType::Image, "Image"},
    {NodeType::Text, "Text"},
    {NodeType::SpectrumBars, "SpectrumBars"},
    {NodeType::AudioSource, "AudioSource"},
}};

constexpr std::array<std::pair<BlendMode, std::string_view>, 6> kBlendModeNames{{
    {BlendMode::Alpha, "Alpha"},
    {BlendMode::PreMulAlpha, "PreMulAlpha"},
    {BlendMode::Add, "Add"},
    {BlendMode::AddAlpha, "AddAlpha"},
    {BlendMode::Screen, "Screen"},
    {BlendMode::Over, "Over"},
}};

constexpr std::array<std::string_view, 3> kScaleModes{"Linear", "Log", "Mel"};

}  // namespace

std::string_view nodeTypeName(NodeType type) {
    for (const auto& [value, name] : kNodeTypeNames) {
        if (value == type) {
            return name;
        }
    }
    return "Unknown";
}

NodeType nodeTypeFromName(std::string_view name) {
    for (const auto& [value, text] : kNodeTypeNames) {
        if (text == name) {
            return value;
        }
    }
    return NodeType::Unknown;
}

std::string_view blendModeName(BlendMode mode) {
    for (const auto& [value, name] : kBlendModeNames) {
        if (value == mode) {
            return name;
        }
    }
    return "Alpha";
}

BlendMode blendModeFromName(std::string_view name) {
    for (const auto& [value, text] : kBlendModeNames) {
        if (text == name) {
            return value;
        }
    }
    return BlendMode::Alpha;
}

void Transform2D::describe(PropertyRegistry& r) {
    r.group("Transform");
    r.point("position", &position);
    r.vec2("scale", &scale);
    r.angle("rotation", &rotation);
    r.point("anchor", &anchor);
    r.range("opacity", &opacity, 0.0f, 1.0f);
}

Node::Node(NodeType type, std::string id) : type_(type), id_(std::move(id)) {}

Node::~Node() = default;

void Node::describe(PropertyRegistry& r) {
    transform.describe(r);
}

Node* Node::addChild(std::unique_ptr<Node> child) {
    return insertChild(children_.size(), std::move(child));
}

Node* Node::insertChild(std::size_t index, std::unique_ptr<Node> child) {
    if (!acceptsChildren() || child == nullptr) {
        return nullptr;
    }
    index = std::min(index, children_.size());
    Node* const bare = child.get();
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
    return bare;
}

std::unique_ptr<Node> Node::removeChild(std::size_t index) {
    if (index >= children_.size()) {
        return nullptr;
    }
    auto at = children_.begin() + static_cast<std::ptrdiff_t>(index);
    auto taken = std::move(*at);
    children_.erase(at);
    return taken;
}

Node* Node::find(std::string_view id) {
    if (id_ == id) {
        return this;
    }
    for (const auto& child : children_) {
        if (Node* const hit = child->find(id)) {
            return hit;
        }
    }
    return nullptr;
}

const Node* Node::find(std::string_view id) const {
    return const_cast<Node*>(this)->find(id);
}

PropertyRegistry Node::describeProperties() {
    PropertyRegistry registry;
    describe(registry);
    return registry;
}

CompositionNode::CompositionNode(std::string id) : Node(NodeType::Composition, std::move(id)) {}

void CompositionNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Composition");
    r.color("background", &background);
    r.range("introFade", &introFade, 0.0f, 10.0f);
    r.range("outroFade", &outroFade, 0.0f, 10.0f);
}

void SolidColorNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Solid");
    r.color("color", &color);
    r.point("size", &size);
}

void ImageNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Image");
    r.color("tint", &tint);
}

void TextNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Text");
    r.range("size", &size, 0.0f, 1.0f);
    r.color("color", &color);
}

std::span<const std::string_view> SpectrumBarsNode::scaleModeOptions() {
    return kScaleModes;
}

void SpectrumBarsNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Shape");
    r.count("barCount", &barCount, 1, 512);
    r.range("barWidth", &barWidth, 0.0f, 1.0f);
    r.range("barSpacing", &barSpacing, 0.0f, 1.0f);
    r.group("Audio");
    r.range("smoothing", &smoothing, 0.0f, 0.99f);
    r.select("scaleMode", &scaleMode, kScaleModes);
    r.group("Look");
    r.color("colorA", &colorA);
    r.color("colorB", &colorB);
}

void AudioSourceNode::describe(PropertyRegistry& r) {
    Node::describe(r);
    r.group("Audio");
    r.range("gain", &gain, 0.0f, 4.0f);
}

std::unique_ptr<Node> makeNode(NodeType type, std::string id) {
    switch (type) {
        case NodeType::Group:
            return std::make_unique<GroupNode>(std::move(id));
        case NodeType::Composition:
            return std::make_unique<CompositionNode>(std::move(id));
        case NodeType::SolidColor:
            return std::make_unique<SolidColorNode>(std::move(id));
        case NodeType::Image:
            return std::make_unique<ImageNode>(std::move(id));
        case NodeType::Text:
            return std::make_unique<TextNode>(std::move(id));
        case NodeType::SpectrumBars:
            return std::make_unique<SpectrumBarsNode>(std::move(id));
        case NodeType::AudioSource:
            return std::make_unique<AudioSourceNode>(std::move(id));
        case NodeType::Unknown:
            break;
    }
    return nullptr;
}

}  // namespace spectra
