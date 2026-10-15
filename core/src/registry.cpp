#include <spectra/core/registry.hpp>

#include <algorithm>

namespace spectra {

namespace {

/// Field-by-field rather than a designated initializer: GCC counts an omitted
/// field as missing even when it has a default member initializer, and
/// `-Wmissing-field-initializers` is an error in CI.
PropertyDesc describe(std::string_view name,
                      PropertyKind kind,
                      PropertyHint hint,
                      void* target,
                      float min = 0.0f,
                      float max = 1.0f,
                      std::span<const std::string_view> options = {}) {
    PropertyDesc desc;
    desc.name = name;
    desc.kind = kind;
    desc.hint = hint;
    desc.target = target;
    desc.min = min;
    desc.max = max;
    desc.options = options;
    return desc;
}

}  // namespace

void PropertyRegistry::add(PropertyDesc desc) {
    desc.group = currentGroup_;
    properties_.push_back(desc);
}

void PropertyRegistry::value(std::string_view name, Property<float>* p) {
    add(describe(name, PropertyKind::Float, PropertyHint::Plain, p));
}

void PropertyRegistry::range(std::string_view name, Property<float>* p, float min, float max) {
    add(describe(name, PropertyKind::Float, PropertyHint::Range, p, min, max));
}

void PropertyRegistry::angle(std::string_view name, Property<float>* p) {
    add(describe(name, PropertyKind::Float, PropertyHint::Angle, p, 0.0f, 360.0f));
}

void PropertyRegistry::point(std::string_view name, Property<Vec2>* p) {
    add(describe(name, PropertyKind::Float2, PropertyHint::Normal, p));
}

void PropertyRegistry::vec2(std::string_view name, Property<Vec2>* p) {
    add(describe(name, PropertyKind::Float2, PropertyHint::Plain, p));
}

void PropertyRegistry::value(std::string_view name, Property<Vec3>* p) {
    add(describe(name, PropertyKind::Float3, PropertyHint::Plain, p));
}

void PropertyRegistry::color(std::string_view name, Property<Color>* p) {
    add(describe(name, PropertyKind::Rgba, PropertyHint::Plain, p));
}

void PropertyRegistry::toggle(std::string_view name, Property<bool>* p) {
    add(describe(name, PropertyKind::Bool, PropertyHint::Plain, p));
}

void PropertyRegistry::count(std::string_view name, Property<int>* p, int min, int max) {
    add(describe(name,
                 PropertyKind::Int,
                 PropertyHint::Range,
                 p,
                 static_cast<float>(min),
                 static_cast<float>(max)));
}

void PropertyRegistry::select(std::string_view name,
                              Property<int>* p,
                              std::span<const std::string_view> options) {
    add(describe(name,
                 PropertyKind::Int,
                 PropertyHint::Select,
                 p,
                 0.0f,
                 static_cast<float>(options.empty() ? 0 : options.size() - 1),
                 options));
}

const PropertyDesc* PropertyRegistry::find(std::string_view name) const {
    const auto at = std::find_if(properties_.begin(),
                                 properties_.end(),
                                 [name](const PropertyDesc& d) { return d.name == name; });
    return at == properties_.end() ? nullptr : &*at;
}

}  // namespace spectra
