#include <spectra/core/evalplan.hpp>

#include <string>

namespace spectra {
namespace {

std::string indexKey(std::string_view nodeId, std::string_view propertyName) {
    std::string key;
    key.reserve(nodeId.size() + propertyName.size() + 1);
    key.append(nodeId);
    key.push_back('\0');
    key.append(propertyName);
    return key;
}

template<typename T>
void writeComponents(const Property<T>& property,
                     Seconds t,
                     const SignalProvider* signals,
                     float* out) {
    const T value = property.evaluate(t, signals);
    if constexpr (std::is_same_v<T, float>) {
        out[0] = value;
    } else if constexpr (std::is_same_v<T, bool>) {
        out[0] = value ? 1.0f : 0.0f;
    } else if constexpr (std::is_same_v<T, int>) {
        out[0] = static_cast<float>(value);
    } else {
        for (glm::length_t i = 0; i < T::length(); ++i) {
            out[static_cast<std::size_t>(i)] = value[i];
        }
    }
}

}  // namespace

void EvalPlan::clear() {
    ops_.clear();
    outputs_.clear();
    nodes_.clear();
    index_.clear();
}

void EvalPlan::build(Node& root) {
    clear();
    addNode(root);
    outputs_.assign(outputs_.size(), 0.0f);
}

void EvalPlan::addNode(Node& node) {
    nodes_.push_back(&node);

    const PropertyRegistry registry = node.describeProperties();
    for (const PropertyDesc& desc : registry.properties()) {
        if (desc.target == nullptr) {
            continue;
        }
        const auto offset = static_cast<std::uint32_t>(outputs_.size());
        const auto count = componentCount(desc.kind);

        ops_.push_back(Op{.target = desc.target, .kind = desc.kind, .offset = offset});
        outputs_.resize(outputs_.size() + count, 0.0f);

        // A node with no id cannot be looked up by name, but still evaluates.
        // Duplicate ids keep the first, which makes a malformed scene render
        // oddly rather than lose properties silently.
        if (!node.id().empty()) {
            index_.emplace(indexKey(node.id(), desc.name), Slot{offset, count});
        }
    }

    for (const auto& child : node.children()) {
        addNode(*child);
    }
}

void EvalPlan::evaluate(Seconds t, const SignalProvider* signals) {
    for (const Op& op : ops_) {
        float* const out = outputs_.data() + op.offset;
        switch (op.kind) {
            case PropertyKind::Float:
                writeComponents(*static_cast<const Property<float>*>(op.target), t, signals, out);
                break;
            case PropertyKind::Float2:
                writeComponents(*static_cast<const Property<Vec2>*>(op.target), t, signals, out);
                break;
            case PropertyKind::Float3:
                writeComponents(*static_cast<const Property<Vec3>*>(op.target), t, signals, out);
                break;
            case PropertyKind::Rgba:
                writeComponents(*static_cast<const Property<Color>*>(op.target), t, signals, out);
                break;
            case PropertyKind::Bool:
                writeComponents(*static_cast<const Property<bool>*>(op.target), t, signals, out);
                break;
            case PropertyKind::Int:
                writeComponents(*static_cast<const Property<int>*>(op.target), t, signals, out);
                break;
        }
    }
}

std::optional<EvalPlan::Slot> EvalPlan::slotOf(std::string_view nodeId,
                                               std::string_view propertyName) const {
    const auto at = index_.find(indexKey(nodeId, propertyName));
    if (at == index_.end()) {
        return std::nullopt;
    }
    return at->second;
}

float EvalPlan::value(std::string_view nodeId, std::string_view propertyName) const {
    const auto slot = slotOf(nodeId, propertyName);
    if (!slot || slot->count < 1) {
        return 0.0f;
    }
    return outputs_[slot->offset];
}

Vec2 EvalPlan::vec2(std::string_view nodeId, std::string_view propertyName) const {
    const auto slot = slotOf(nodeId, propertyName);
    if (!slot || slot->count < 2) {
        return Vec2{0.0f};
    }
    return Vec2{outputs_[slot->offset], outputs_[slot->offset + 1]};
}

Color EvalPlan::color(std::string_view nodeId, std::string_view propertyName) const {
    const auto slot = slotOf(nodeId, propertyName);
    if (!slot || slot->count < 4) {
        return Color{0.0f};
    }
    return Color{outputs_[slot->offset],
                 outputs_[slot->offset + 1],
                 outputs_[slot->offset + 2],
                 outputs_[slot->offset + 3]};
}

}  // namespace spectra
