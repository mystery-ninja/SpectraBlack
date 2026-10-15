#include <spectra/core/json_extras.hpp>
#include <spectra/core/registry.hpp>
#include <spectra/core/serialization.hpp>
#include <spectra/core/version.hpp>

#include <array>
#include <string>
#include <utility>

namespace spectra {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------- enum names

template<typename E, std::size_t N>
std::string_view nameOf(const std::array<std::pair<E, std::string_view>, N>& table,
                        E value,
                        std::string_view fallback) {
    for (const auto& [candidate, text] : table) {
        if (candidate == value) {
            return text;
        }
    }
    return fallback;
}

template<typename E, std::size_t N>
E valueOf(const std::array<std::pair<E, std::string_view>, N>& table,
          std::string_view name,
          E fallback) {
    for (const auto& [candidate, text] : table) {
        if (text == name) {
            return candidate;
        }
    }
    return fallback;
}

constexpr std::array<std::pair<Interpolation, std::string_view>, 4> kInterpolationNames{{
    {Interpolation::Hold, "Hold"},
    {Interpolation::Linear, "Linear"},
    {Interpolation::Bezier, "Bezier"},
    {Interpolation::Ease, "Ease"},
}};

constexpr std::array<std::pair<BlendOp, std::string_view>, 5> kBlendOpNames{{
    {BlendOp::Add, "Add"},
    {BlendOp::Multiply, "Multiply"},
    {BlendOp::Replace, "Replace"},
    {BlendOp::Max, "Max"},
    {BlendOp::Min, "Min"},
}};

constexpr std::array<std::pair<CurveKind, std::string_view>, 3> kCurveKindNames{{
    {CurveKind::Linear, "Linear"},
    {CurveKind::Exponential, "Exponential"},
    {CurveKind::Smoothstep, "Smoothstep"},
}};

// --------------------------------------------------------------- value codec

json encodeValue(float v) {
    return v;
}

json encodeValue(bool v) {
    return v;
}

json encodeValue(int v) {
    return v;
}

json encodeValue(const Vec2& v) {
    return json::array({v.x, v.y});
}

json encodeValue(const Vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json encodeValue(const Color& v) {
    return json::array({v.r, v.g, v.b, v.a});
}

void decodeValue(const json& j, float& out) {
    if (j.is_number()) {
        out = j.get<float>();
    }
}

void decodeValue(const json& j, bool& out) {
    if (j.is_boolean()) {
        out = j.get<bool>();
    }
}

void decodeValue(const json& j, int& out) {
    if (j.is_number()) {
        out = j.get<int>();
    }
}

template<glm::length_t N, typename S, glm::qualifier Q>
void decodeValue(const json& j, glm::vec<N, S, Q>& out) {
    if (!j.is_array()) {
        return;
    }
    const auto limit = std::min<std::size_t>(j.size(), static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < limit; ++i) {
        if (j[i].is_number()) {
            out[static_cast<glm::length_t>(i)] = j[i].get<S>();
        }
    }
}

// ------------------------------------------------------------------ keyframes

template<typename T>
json encodeKeyframe(const Keyframe<T>& key) {
    json j;
    j["t"] = key.time;
    j["v"] = encodeValue(key.value);
    j["in"] = nameOf(kInterpolationNames, key.in, "Bezier");
    j["out"] = nameOf(kInterpolationNames, key.out, "Bezier");

    // Handles only matter for explicit beziers, and only when they are not the
    // default. Omitting them keeps the common file small and readable.
    if (key.in == Interpolation::Bezier && !(key.handleIn == BezierHandle{})) {
        j["hi"] = json::array({key.handleIn.x, key.handleIn.y});
    }
    if (key.out == Interpolation::Bezier && !(key.handleOut == BezierHandle{})) {
        j["ho"] = json::array({key.handleOut.x, key.handleOut.y});
    }
    return j;
}

void decodeHandle(const json& j, BezierHandle& out) {
    if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number()) {
        out.x = j[0].get<float>();
        out.y = j[1].get<float>();
    }
}

template<typename T>
Keyframe<T> decodeKeyframe(const json& j) {
    Keyframe<T> key;
    key.time = j.value("t", 0.0);
    if (j.contains("v")) {
        decodeValue(j["v"], key.value);
    }
    key.in =
        valueOf(kInterpolationNames, j.value("in", std::string("Bezier")), Interpolation::Bezier);
    key.out =
        valueOf(kInterpolationNames, j.value("out", std::string("Bezier")), Interpolation::Bezier);
    if (j.contains("hi")) {
        decodeHandle(j["hi"], key.handleIn);
    }
    if (j.contains("ho")) {
        decodeHandle(j["ho"], key.handleOut);
    }
    return key;
}

// -------------------------------------------------------------------- drivers

json encodeShaping(const ShapingChain& chain) {
    json j = json::object();

    const Remap defaultRemap;
    if (chain.remap.inMin != defaultRemap.inMin || chain.remap.inMax != defaultRemap.inMax ||
        chain.remap.outMin != defaultRemap.outMin || chain.remap.outMax != defaultRemap.outMax ||
        chain.remap.invert != defaultRemap.invert) {
        j["remap"] = {{"inMin", chain.remap.inMin},
                      {"inMax", chain.remap.inMax},
                      {"outMin", chain.remap.outMin},
                      {"outMax", chain.remap.outMax},
                      {"invert", chain.remap.invert}};
    }
    if (chain.curve.kind != CurveKind::Linear) {
        j["curve"] = {{"kind", nameOf(kCurveKindNames, chain.curve.kind, "Linear")},
                      {"exponent", chain.curve.exponent}};
    }
    if (chain.envelope.active()) {
        j["envelope"] = {{"attack", chain.envelope.attack}, {"decay", chain.envelope.decay}};
    }
    if (chain.delaySeconds != 0.0) {
        j["delay"] = chain.delaySeconds;
    }
    if (chain.random.enabled) {
        j["random"] = {{"seed", chain.random.seed}, {"amount", chain.random.amount}};
    }
    return j;
}

ShapingChain decodeShaping(const json& j) {
    ShapingChain chain;
    if (!j.is_object()) {
        return chain;
    }
    if (const auto at = j.find("remap"); at != j.end() && at->is_object()) {
        chain.remap.inMin = at->value("inMin", chain.remap.inMin);
        chain.remap.inMax = at->value("inMax", chain.remap.inMax);
        chain.remap.outMin = at->value("outMin", chain.remap.outMin);
        chain.remap.outMax = at->value("outMax", chain.remap.outMax);
        chain.remap.invert = at->value("invert", chain.remap.invert);
    }
    if (const auto at = j.find("curve"); at != j.end() && at->is_object()) {
        chain.curve.kind =
            valueOf(kCurveKindNames, at->value("kind", std::string("Linear")), CurveKind::Linear);
        chain.curve.exponent = at->value("exponent", chain.curve.exponent);
    }
    if (const auto at = j.find("envelope"); at != j.end() && at->is_object()) {
        chain.envelope.attack = at->value("attack", 0.0f);
        chain.envelope.decay = at->value("decay", 0.0f);
    }
    chain.delaySeconds = j.value("delay", 0.0);
    if (const auto at = j.find("random"); at != j.end() && at->is_object()) {
        chain.random.enabled = true;
        chain.random.seed = at->value("seed", 0u);
        chain.random.amount = at->value("amount", 1.0f);
    }
    return chain;
}

json encodeDriver(const Driver& driver) {
    json j;
    j["signal"] = driver.signal.name;
    if (driver.signal.argA != 0.0f) {
        j["argA"] = driver.signal.argA;
    }
    if (driver.signal.argB != 0.0f) {
        j["argB"] = driver.signal.argB;
    }
    j["blend"] = nameOf(kBlendOpNames, driver.blend, "Add");
    j["amount"] = driver.amount;
    if (!driver.enabled) {
        j["enabled"] = false;
    }
    if (json shaping = encodeShaping(driver.shaping); !shaping.empty()) {
        j["shaping"] = std::move(shaping);
    }
    return j;
}

Driver decodeDriver(const json& j) {
    Driver driver;
    driver.signal.name = j.value("signal", std::string());
    driver.signal.argA = j.value("argA", 0.0f);
    driver.signal.argB = j.value("argB", 0.0f);
    driver.blend = valueOf(kBlendOpNames, j.value("blend", std::string("Add")), BlendOp::Add);
    driver.amount = j.value("amount", 1.0f);
    driver.enabled = j.value("enabled", true);
    if (const auto at = j.find("shaping"); at != j.end()) {
        driver.shaping = decodeShaping(*at);
    }
    return driver;
}

// ----------------------------------------------------------------- properties

template<typename T>
json encodeProperty(const Property<T>& property) {
    json j;
    j["static"] = encodeValue(property.staticValue);

    if (!property.keyframes.empty()) {
        json keys = json::array();
        for (const Keyframe<T>& key : property.keyframes.keys()) {
            keys.push_back(encodeKeyframe(key));
        }
        j["keyframes"] = std::move(keys);
    }
    if (!property.drivers.empty()) {
        json drivers = json::array();
        for (const Driver& driver : property.drivers) {
            drivers.push_back(encodeDriver(driver));
        }
        j["drivers"] = std::move(drivers);
    }
    return j;
}

template<typename T>
void decodeProperty(const json& j, Property<T>& property) {
    if (!j.is_object()) {
        return;
    }
    if (j.contains("static")) {
        decodeValue(j["static"], property.staticValue);
    }
    if (const auto at = j.find("keyframes"); at != j.end() && at->is_array()) {
        property.keyframes.clear();
        for (const json& key : *at) {
            property.keyframes.insert(decodeKeyframe<T>(key));
        }
    }
    if (const auto at = j.find("drivers"); at != j.end() && at->is_array()) {
        property.drivers.clear();
        for (const json& driver : *at) {
            property.drivers.push_back(decodeDriver(driver));
        }
    }
}

/// Dispatch on the descriptor's kind. This switch is the one place that has to
/// know every property type, and adding a kind breaks it loudly at compile
/// time rather than quietly dropping the property from saved files.
json encodeDescribed(const PropertyDesc& desc) {
    switch (desc.kind) {
        case PropertyKind::Float:
            return encodeProperty(*propertyCast<float>(desc));
        case PropertyKind::Float2:
            return encodeProperty(*propertyCast<Vec2>(desc));
        case PropertyKind::Float3:
            return encodeProperty(*propertyCast<Vec3>(desc));
        case PropertyKind::Rgba:
            return encodeProperty(*propertyCast<Color>(desc));
        case PropertyKind::Bool:
            return encodeProperty(*propertyCast<bool>(desc));
        case PropertyKind::Int:
            return encodeProperty(*propertyCast<int>(desc));
    }
    return json::object();
}

void decodeDescribed(const json& j, const PropertyDesc& desc) {
    switch (desc.kind) {
        case PropertyKind::Float:
            decodeProperty(j, *propertyCast<float>(desc));
            return;
        case PropertyKind::Float2:
            decodeProperty(j, *propertyCast<Vec2>(desc));
            return;
        case PropertyKind::Float3:
            decodeProperty(j, *propertyCast<Vec3>(desc));
            return;
        case PropertyKind::Rgba:
            decodeProperty(j, *propertyCast<Color>(desc));
            return;
        case PropertyKind::Bool:
            decodeProperty(j, *propertyCast<bool>(desc));
            return;
        case PropertyKind::Int:
            decodeProperty(j, *propertyCast<int>(desc));
            return;
    }
}

// ---------------------------------------------------------------------- nodes

constexpr std::array<std::string_view, 10> kKnownNodeKeys{"id",
                                                          "type",
                                                          "name",
                                                          "enabled",
                                                          "blendMode",
                                                          "properties",
                                                          "children",
                                                          "source",
                                                          "text",
                                                          "fontFamily"};

bool isKnownNodeKey(std::string_view key) {
    for (std::string_view known : kKnownNodeKeys) {
        if (known == key) {
            return true;
        }
    }
    return false;
}

json encodeNode(Node& node);

json encodeNodeChildren(Node& node) {
    json children = json::array();
    for (const auto& child : node.children()) {
        children.push_back(encodeNode(*child));
    }
    return children;
}

json encodeNode(Node& node) {
    // An unrecognised node is written back exactly as it was read. Any attempt
    // to reconstruct it from the fields we understood would drop the ones we
    // did not, which is the whole failure this type exists to prevent.
    if (node.type() == NodeType::Unknown) {
        const auto& unknown = static_cast<const UnknownNode&>(node);
        if (node.extras != nullptr && node.extras->data.is_object()) {
            return node.extras->data;
        }
        json j;
        j["id"] = node.id();
        j["type"] = unknown.originalType;
        return j;
    }

    json j;
    j["id"] = node.id();
    j["type"] = nodeTypeName(node.type());
    j["name"] = node.name;
    j["enabled"] = node.enabled;
    j["blendMode"] = blendModeName(node.blendMode);

    switch (node.type()) {
        case NodeType::Image:
            j["source"] = static_cast<const ImageNode&>(node).source;
            break;
        case NodeType::AudioSource:
            j["source"] = static_cast<const AudioSourceNode&>(node).source;
            break;
        case NodeType::Text: {
            const auto& text = static_cast<const TextNode&>(node);
            j["text"] = text.text;
            j["fontFamily"] = text.fontFamily;
            break;
        }
        default:
            break;
    }

    PropertyRegistry registry = node.describeProperties();
    json properties = json::object();
    for (const PropertyDesc& desc : registry.properties()) {
        if (desc.target != nullptr) {
            properties[std::string(desc.name)] = encodeDescribed(desc);
        }
    }

    // Property keys from a newer build get written back alongside ours, so a
    // round trip through this version does not strip them.
    if (node.extras != nullptr && node.extras->data.is_object()) {
        if (const auto at = node.extras->data.find("properties");
            at != node.extras->data.end() && at->is_object()) {
            for (const auto& [key, value] : at->items()) {
                if (!properties.contains(key)) {
                    properties[key] = value;
                }
            }
        }
    }
    j["properties"] = std::move(properties);

    if (node.acceptsChildren()) {
        j["children"] = encodeNodeChildren(node);
    }

    if (node.extras != nullptr && node.extras->data.is_object()) {
        for (const auto& [key, value] : node.extras->data.items()) {
            if (!isKnownNodeKey(key)) {
                j[key] = value;
            }
        }
    }
    return j;
}

std::unique_ptr<Node> decodeNode(const json& j) {
    if (!j.is_object()) {
        return nullptr;
    }

    const auto typeName = j.value("type", std::string("Unknown"));
    const auto id = j.value("id", std::string());
    const auto type = nodeTypeFromName(typeName);

    if (type == NodeType::Unknown) {
        auto node = std::make_unique<UnknownNode>(id);
        node->originalType = typeName;
        node->name = j.value("name", typeName);
        node->extras = std::make_shared<JsonExtras>(j);
        return node;
    }

    auto node = makeNode(type, id);
    if (node == nullptr) {
        return nullptr;
    }

    node->name = j.value("name", std::string());
    node->enabled = j.value("enabled", true);
    node->blendMode = blendModeFromName(j.value("blendMode", std::string("Alpha")));

    switch (type) {
        case NodeType::Image:
            static_cast<ImageNode&>(*node).source = j.value("source", std::string());
            break;
        case NodeType::AudioSource:
            static_cast<AudioSourceNode&>(*node).source = j.value("source", std::string());
            break;
        case NodeType::Text: {
            auto& text = static_cast<TextNode&>(*node);
            text.text = j.value("text", std::string());
            text.fontFamily = j.value("fontFamily", std::string());
            break;
        }
        default:
            break;
    }

    json unknown = json::object();

    PropertyRegistry registry = node->describeProperties();
    if (const auto at = j.find("properties"); at != j.end() && at->is_object()) {
        json unknownProperties = json::object();
        for (const auto& [key, value] : at->items()) {
            if (const PropertyDesc* const desc = registry.find(key)) {
                decodeDescribed(value, *desc);
            } else {
                unknownProperties[key] = value;
            }
        }
        if (!unknownProperties.empty()) {
            unknown["properties"] = std::move(unknownProperties);
        }
    }

    if (node->acceptsChildren()) {
        if (const auto at = j.find("children"); at != j.end() && at->is_array()) {
            for (const json& child : *at) {
                node->addChild(decodeNode(child));
            }
        }
    }

    for (const auto& [key, value] : j.items()) {
        if (!isKnownNodeKey(key)) {
            unknown[key] = value;
        }
    }
    if (!unknown.empty()) {
        node->extras = std::make_shared<JsonExtras>(std::move(unknown));
    }
    return node;
}

// ---------------------------------------------------------------------- scene

constexpr std::array<std::string_view, 6> kKnownSceneKeys{
    "format", "formatVersion", "appVersion", "project", "audio", "compositions"};

bool isKnownSceneKey(std::string_view key) {
    for (std::string_view known : kKnownSceneKeys) {
        if (known == key) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string toJson(Scene& scene, int indent) {
    json j;
    j["format"] = std::string(kFormatTag);
    j["formatVersion"] = kFormatVersion;
    j["appVersion"] = std::string(versionString());

    j["project"] = {{"name", scene.project.name},
                    {"resolution", json::array({scene.project.width, scene.project.height})},
                    {"frameRate", scene.project.frameRate},
                    {"duration", scene.project.duration},
                    {"colorSpace", scene.project.colorSpace}};

    j["audio"] = {
        {"source", scene.audio.source},
        {"analysis",
         {{"bandCount", scene.audio.analysis.bandCount},
          {"bandScale", scene.audio.analysis.bandScale},
          {"attack", scene.audio.analysis.attack},
          {"decay", scene.audio.analysis.decay},
          {"normalization", scene.audio.analysis.normalization},
          {"tilt", scene.audio.analysis.tilt}}},
        {"beatGrid", {{"bpm", scene.audio.beatGrid.bpm}, {"offset", scene.audio.beatGrid.offset}}}};

    json compositions = json::array();
    for (const auto& composition : scene.compositions()) {
        compositions.push_back(encodeNode(*composition));
    }
    j["compositions"] = std::move(compositions);

    if (scene.extras != nullptr && scene.extras->data.is_object()) {
        for (const auto& [key, value] : scene.extras->data.items()) {
            if (!isKnownSceneKey(key)) {
                j[key] = value;
            }
        }
    }

    return j.dump(indent);
}

Result<std::unique_ptr<Scene>> fromJson(std::string_view text) {
    using ReturnType = Result<std::unique_ptr<Scene>>;

    // nlohmann's throwing parser is off; core does not use exceptions.
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        return ReturnType::error("not valid JSON");
    }
    if (!j.is_object()) {
        return ReturnType::error("project root must be a JSON object");
    }

    const auto format = j.value("format", std::string());
    if (format != kFormatTag) {
        return ReturnType::error("not a Spectra Black project (format is \"" + format + "\")");
    }

    const int version = j.value("formatVersion", 0);
    if (version > kFormatVersion) {
        return ReturnType::error("project was saved by a newer version (format version " +
                                 std::to_string(version) + ", this build understands " +
                                 std::to_string(kFormatVersion) + ")");
    }

    auto scene = std::make_unique<Scene>();

    if (const auto at = j.find("project"); at != j.end() && at->is_object()) {
        scene->project.name = at->value("name", scene->project.name);
        scene->project.frameRate = at->value("frameRate", scene->project.frameRate);
        scene->project.duration = at->value("duration", scene->project.duration);
        scene->project.colorSpace = at->value("colorSpace", scene->project.colorSpace);
        if (const auto res = at->find("resolution");
            res != at->end() && res->is_array() && res->size() >= 2) {
            scene->project.width = (*res)[0].get<int>();
            scene->project.height = (*res)[1].get<int>();
        }
    }

    if (const auto at = j.find("audio"); at != j.end() && at->is_object()) {
        scene->audio.source = at->value("source", std::string());
        if (const auto analysis = at->find("analysis");
            analysis != at->end() && analysis->is_object()) {
            auto& a = scene->audio.analysis;
            a.bandCount = analysis->value("bandCount", a.bandCount);
            a.bandScale = analysis->value("bandScale", a.bandScale);
            a.attack = analysis->value("attack", a.attack);
            a.decay = analysis->value("decay", a.decay);
            a.normalization = analysis->value("normalization", a.normalization);
            a.tilt = analysis->value("tilt", a.tilt);
        }
        if (const auto grid = at->find("beatGrid"); grid != at->end() && grid->is_object()) {
            scene->audio.beatGrid.bpm = grid->value("bpm", 0.0);
            scene->audio.beatGrid.offset = grid->value("offset", 0.0);
        }
    }

    if (const auto at = j.find("compositions"); at != j.end() && at->is_array()) {
        for (const json& composition : *at) {
            scene->addComposition(decodeNode(composition));
        }
    }

    json unknown = json::object();
    for (const auto& [key, value] : j.items()) {
        if (!isKnownSceneKey(key)) {
            unknown[key] = value;
        }
    }
    if (!unknown.empty()) {
        scene->extras = std::make_shared<JsonExtras>(std::move(unknown));
    }

    return ReturnType::ok(std::move(scene));
}

}  // namespace spectra
