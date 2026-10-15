#pragma once

#include <spectra/core/property.hpp>
#include <spectra/core/types.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spectra {

/// What a property stores, and therefore how many floats it occupies in the
/// evaluation buffer.
///
/// Named by component count rather than reusing `Vec2`/`Vec3`/`Color` from
/// `types.hpp`: GCC reports an enumerator matching a type alias as a shadowed
/// declaration (PR 78147), and `-Wshadow` is an error in CI.
enum class PropertyKind : std::uint8_t {
    Float,
    Float2,
    Float3,
    Rgba,
    Bool,
    Int,
};

/// How the inspector should present a property. Purely a display hint — it
/// never changes what is stored or how the value evaluates.
enum class PropertyHint : std::uint8_t {
    Plain,
    Range,   ///< Slider between `min` and `max`.
    Angle,   ///< Degrees, wrapping.
    Select,  ///< One of `options`, stored as an index.
    Normal,  ///< Normalized screen-space coordinate.
};

/// One registered parameter of a node.
///
/// `target` points at the node's own `Property<T>`; nothing is copied. The
/// descriptor is a *view* of the node, produced on demand by `describe()`.
struct PropertyDesc {
    std::string_view name;   ///< Stable identifier. This is the save-format key.
    std::string_view group;  ///< Inspector category, in declaration order.
    PropertyKind kind{PropertyKind::Float};
    PropertyHint hint{PropertyHint::Plain};
    void* target{nullptr};
    float min{0.0f};
    float max{1.0f};

    std::span<const std::string_view> options;  ///< Only for `Select`.
};

/// Collects a node's parameters.
///
/// Nodes implement `describe(PropertyRegistry&)` with one call per parameter,
/// and that single declaration drives the inspector UI, keyframe support,
/// driver support, serialization, and the save-format name. The point is that
/// they cannot drift apart, because there is only one of them.
///
/// Avee does the same thing with `onReadCustomization` / `onApplyCustomization`
/// — but as *two* functions that must be kept in sync by hand, which is
/// visibly a bug source. See docs/09-AVEE-TEARDOWN.md.
class PropertyRegistry {
public:
    /// Begin a category. Applies to every parameter declared after it.
    void group(std::string_view name) { currentGroup_ = name; }

    void value(std::string_view name, Property<float>* p);
    void range(std::string_view name, Property<float>* p, float min, float max);
    void angle(std::string_view name, Property<float>* p);
    void point(std::string_view name, Property<Vec2>* p);
    void vec2(std::string_view name, Property<Vec2>* p);
    void value(std::string_view name, Property<Vec3>* p);
    void color(std::string_view name, Property<Color>* p);
    void toggle(std::string_view name, Property<bool>* p);
    void count(std::string_view name, Property<int>* p, int min, int max);
    void select(std::string_view name, Property<int>* p, std::span<const std::string_view> options);

    [[nodiscard]] const std::vector<PropertyDesc>& properties() const { return properties_; }

    /// Look a parameter up by its stable name. Null when absent, which is the
    /// normal result when loading a file written by a different version.
    [[nodiscard]] const PropertyDesc* find(std::string_view name) const;

    [[nodiscard]] bool empty() const { return properties_.empty(); }

private:
    void add(PropertyDesc desc);

    std::vector<PropertyDesc> properties_;
    std::string_view currentGroup_{"General"};
};

/// The property kind that stores a given C++ type.
template<typename T>
inline constexpr PropertyKind kPropertyKind = PropertyKind::Float;
template<>
inline constexpr PropertyKind kPropertyKind<Vec2> = PropertyKind::Float2;
template<>
inline constexpr PropertyKind kPropertyKind<Vec3> = PropertyKind::Float3;
template<>
inline constexpr PropertyKind kPropertyKind<Color> = PropertyKind::Rgba;
template<>
inline constexpr PropertyKind kPropertyKind<bool> = PropertyKind::Bool;
template<>
inline constexpr PropertyKind kPropertyKind<int> = PropertyKind::Int;

/// Recover the typed property behind a descriptor.
///
/// Returns null if `T` does not match the descriptor's kind, so a caller that
/// guesses wrong gets a null pointer rather than a reinterpreted object. There
/// is no RTTI in this project; the kind tag is the type information.
template<typename T>
[[nodiscard]] Property<T>* propertyCast(const PropertyDesc& desc) {
    if (desc.kind != kPropertyKind<T>) {
        return nullptr;
    }
    return static_cast<Property<T>*>(desc.target);
}

/// Number of float components a kind occupies in a flat evaluation buffer.
[[nodiscard]] constexpr std::uint32_t componentCount(PropertyKind kind) {
    switch (kind) {
        case PropertyKind::Float2:
            return 2;
        case PropertyKind::Float3:
            return 3;
        case PropertyKind::Rgba:
            return 4;
        case PropertyKind::Float:
        case PropertyKind::Bool:
        case PropertyKind::Int:
            return 1;
    }
    return 1;
}

}  // namespace spectra
