#include <spectra/core/registry.hpp>

#include <algorithm>

namespace spectra {

void PropertyRegistry::add(PropertyDesc desc) {
    desc.group = currentGroup_;
    properties_.push_back(desc);
}

void PropertyRegistry::value(std::string_view name, Property<float>* p) {
    add({.name = name, .kind = PropertyKind::Float, .hint = PropertyHint::Plain, .target = p});
}

void PropertyRegistry::range(std::string_view name, Property<float>* p, float min, float max) {
    add({.name = name,
         .kind = PropertyKind::Float,
         .hint = PropertyHint::Range,
         .target = p,
         .min = min,
         .max = max});
}

void PropertyRegistry::angle(std::string_view name, Property<float>* p) {
    add({.name = name,
         .kind = PropertyKind::Float,
         .hint = PropertyHint::Angle,
         .target = p,
         .min = 0.0f,
         .max = 360.0f});
}

void PropertyRegistry::point(std::string_view name, Property<Vec2>* p) {
    add({.name = name, .kind = PropertyKind::Float2, .hint = PropertyHint::Normal, .target = p});
}

void PropertyRegistry::vec2(std::string_view name, Property<Vec2>* p) {
    add({.name = name, .kind = PropertyKind::Float2, .hint = PropertyHint::Plain, .target = p});
}

void PropertyRegistry::value(std::string_view name, Property<Vec3>* p) {
    add({.name = name, .kind = PropertyKind::Float3, .hint = PropertyHint::Plain, .target = p});
}

void PropertyRegistry::color(std::string_view name, Property<Color>* p) {
    add({.name = name, .kind = PropertyKind::Rgba, .hint = PropertyHint::Plain, .target = p});
}

void PropertyRegistry::toggle(std::string_view name, Property<bool>* p) {
    add({.name = name, .kind = PropertyKind::Bool, .hint = PropertyHint::Plain, .target = p});
}

void PropertyRegistry::count(std::string_view name, Property<int>* p, int min, int max) {
    add({.name = name,
         .kind = PropertyKind::Int,
         .hint = PropertyHint::Range,
         .target = p,
         .min = static_cast<float>(min),
         .max = static_cast<float>(max)});
}

void PropertyRegistry::select(std::string_view name,
                              Property<int>* p,
                              std::span<const std::string_view> options) {
    add({.name = name,
         .kind = PropertyKind::Int,
         .hint = PropertyHint::Select,
         .target = p,
         .min = 0.0f,
         .max = static_cast<float>(options.empty() ? 0 : options.size() - 1),
         .options = options});
}

const PropertyDesc* PropertyRegistry::find(std::string_view name) const {
    const auto at = std::find_if(properties_.begin(),
                                 properties_.end(),
                                 [name](const PropertyDesc& d) { return d.name == name; });
    return at == properties_.end() ? nullptr : &*at;
}

}  // namespace spectra
