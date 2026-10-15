#include <spectra/core/node.hpp>
#include <spectra/core/registry.hpp>
#include <spectra/core/scene.hpp>

#include <doctest/doctest.h>

#include <ostream>

using namespace spectra;

TEST_CASE("node type names round-trip") {
    for (const NodeType type : {NodeType::Group,
                                NodeType::Composition,
                                NodeType::SolidColor,
                                NodeType::Image,
                                NodeType::Text,
                                NodeType::SpectrumBars,
                                NodeType::AudioSource}) {
        CHECK(nodeTypeFromName(nodeTypeName(type)) == type);
    }
}

TEST_CASE("an unrecognised type name becomes Unknown rather than a wrong type") {
    CHECK(nodeTypeFromName("NodeTypeFromTheFuture") == NodeType::Unknown);
}

TEST_CASE("blend mode names round-trip") {
    for (const BlendMode mode : {BlendMode::Alpha,
                                 BlendMode::PreMulAlpha,
                                 BlendMode::Add,
                                 BlendMode::AddAlpha,
                                 BlendMode::Screen,
                                 BlendMode::Over}) {
        CHECK(blendModeFromName(blendModeName(mode)) == mode);
    }
}

TEST_CASE("only container nodes accept children") {
    GroupNode group;
    CompositionNode composition;
    SolidColorNode solid;

    CHECK(group.acceptsChildren());
    CHECK(composition.acceptsChildren());
    CHECK_FALSE(solid.acceptsChildren());

    CHECK(group.addChild(std::make_unique<SolidColorNode>("a")) != nullptr);
    CHECK(solid.addChild(std::make_unique<SolidColorNode>("b")) == nullptr);
    CHECK(group.children().size() == 1);
    CHECK(solid.children().empty());
}

TEST_CASE("adding a null child is ignored rather than crashing") {
    GroupNode group;
    CHECK(group.addChild(nullptr) == nullptr);
    CHECK(group.children().empty());
}

TEST_CASE("children insert at a position and clamp past the end") {
    GroupNode group;
    group.addChild(std::make_unique<SolidColorNode>("a"));
    group.addChild(std::make_unique<SolidColorNode>("c"));
    group.insertChild(1, std::make_unique<SolidColorNode>("b"));
    group.insertChild(999, std::make_unique<SolidColorNode>("d"));

    REQUIRE(group.children().size() == 4);
    CHECK(group.children()[0]->id() == "a");
    CHECK(group.children()[1]->id() == "b");
    CHECK(group.children()[2]->id() == "c");
    CHECK(group.children()[3]->id() == "d");
}

TEST_CASE("removing a child hands back ownership") {
    GroupNode group;
    group.addChild(std::make_unique<SolidColorNode>("a"));
    group.addChild(std::make_unique<SolidColorNode>("b"));

    auto taken = group.removeChild(0);
    REQUIRE(taken != nullptr);
    CHECK(taken->id() == "a");
    CHECK(group.children().size() == 1);
    CHECK(group.removeChild(99) == nullptr);
}

TEST_CASE("find searches the whole subtree") {
    GroupNode root("root");
    Node* const inner = root.addChild(std::make_unique<GroupNode>("inner"));
    inner->addChild(std::make_unique<SolidColorNode>("deep"));

    CHECK(root.find("root") == &root);
    CHECK(root.find("inner") == inner);
    REQUIRE(root.find("deep") != nullptr);
    CHECK(root.find("deep")->id() == "deep");
    CHECK(root.find("absent") == nullptr);
}

TEST_CASE("every node registers its transform first") {
    SolidColorNode solid;
    PropertyRegistry registry = solid.describeProperties();

    REQUIRE(registry.properties().size() >= 5);
    CHECK(registry.properties()[0].name == "position");
    CHECK(registry.properties()[0].group == "Transform");

    for (const char* name : {"position", "scale", "rotation", "anchor", "opacity"}) {
        CHECK(registry.find(name) != nullptr);
    }
}

TEST_CASE("a node's own properties follow its transform") {
    SpectrumBarsNode bars;
    PropertyRegistry registry = bars.describeProperties();

    for (const char* name :
         {"barCount", "barWidth", "barSpacing", "smoothing", "scaleMode", "colorA", "colorB"}) {
        CHECK(registry.find(name) != nullptr);
    }
    CHECK(registry.find("barCount")->group == "Shape");
    CHECK(registry.find("smoothing")->group == "Audio");
    CHECK(registry.find("colorA")->group == "Look");
}

TEST_CASE("descriptors point at the node's real properties") {
    SpectrumBarsNode bars;
    PropertyRegistry registry = bars.describeProperties();

    const PropertyDesc* const desc = registry.find("barWidth");
    REQUIRE(desc != nullptr);
    Property<float>* const property = propertyCast<float>(*desc);
    REQUIRE(property == &bars.barWidth);

    property->staticValue = 0.25f;
    CHECK(bars.barWidth.staticValue == doctest::Approx(0.25f));
}

TEST_CASE("casting a descriptor to the wrong type gives null, not garbage") {
    SpectrumBarsNode bars;
    PropertyRegistry registry = bars.describeProperties();

    const PropertyDesc* const desc = registry.find("colorA");
    REQUIRE(desc != nullptr);
    CHECK(propertyCast<Color>(*desc) == &bars.colorA);
    CHECK(propertyCast<float>(*desc) == nullptr);
    CHECK(propertyCast<Vec2>(*desc) == nullptr);
    CHECK(propertyCast<int>(*desc) == nullptr);
}

TEST_CASE("range and select hints carry their bounds") {
    SpectrumBarsNode bars;
    PropertyRegistry registry = bars.describeProperties();

    const PropertyDesc* const count = registry.find("barCount");
    REQUIRE(count != nullptr);
    CHECK(count->kind == PropertyKind::Int);
    CHECK(count->hint == PropertyHint::Range);
    CHECK(count->min == doctest::Approx(1.0f));
    CHECK(count->max == doctest::Approx(512.0f));

    const PropertyDesc* const mode = registry.find("scaleMode");
    REQUIRE(mode != nullptr);
    CHECK(mode->hint == PropertyHint::Select);
    REQUIRE(mode->options.size() == 3);
    CHECK(mode->options[0] == "Linear");
    CHECK(mode->options[2] == "Mel");
}

TEST_CASE("looking up an absent property gives null") {
    SolidColorNode solid;
    PropertyRegistry registry = solid.describeProperties();
    CHECK(registry.find("propertyFromAFutureVersion") == nullptr);
}

TEST_CASE("component counts match the kinds") {
    CHECK(componentCount(PropertyKind::Float) == 1);
    CHECK(componentCount(PropertyKind::Bool) == 1);
    CHECK(componentCount(PropertyKind::Int) == 1);
    CHECK(componentCount(PropertyKind::Float2) == 2);
    CHECK(componentCount(PropertyKind::Float3) == 3);
    CHECK(componentCount(PropertyKind::Rgba) == 4);
}

TEST_CASE("makeNode builds every known type and refuses Unknown") {
    for (const NodeType type : {NodeType::Group,
                                NodeType::Composition,
                                NodeType::SolidColor,
                                NodeType::Image,
                                NodeType::Text,
                                NodeType::SpectrumBars,
                                NodeType::AudioSource}) {
        auto node = makeNode(type, "x");
        REQUIRE(node != nullptr);
        CHECK(node->type() == type);
        CHECK(node->id() == "x");
    }
    // Unknown needs the original type name, so only the loader can build it.
    CHECK(makeNode(NodeType::Unknown, "x") == nullptr);
}

TEST_CASE("scene composition list behaves like the node child list") {
    Scene scene;
    scene.addComposition(std::make_unique<CompositionNode>("one"));
    scene.addComposition(std::make_unique<CompositionNode>("two"));

    REQUIRE(scene.compositions().size() == 2);
    CHECK(scene.find("one") != nullptr);
    CHECK(scene.find("missing") == nullptr);

    auto taken = scene.removeComposition(0);
    REQUIRE(taken != nullptr);
    CHECK(taken->id() == "one");
    CHECK(scene.find("one") == nullptr);
}

TEST_CASE("scene find reaches into compositions") {
    Scene scene;
    Node* const composition = scene.addComposition(std::make_unique<CompositionNode>("main"));
    composition->addChild(std::make_unique<SpectrumBarsNode>("bars"));

    REQUIRE(scene.find("bars") != nullptr);
    CHECK(scene.find("bars")->type() == NodeType::SpectrumBars);
}

TEST_CASE("generated ids do not collide with existing ones") {
    Scene scene;
    Node* const composition = scene.addComposition(std::make_unique<CompositionNode>("main"));
    composition->addChild(std::make_unique<SolidColorNode>("layer_1"));

    const std::string generated = scene.makeUniqueId("layer");
    CHECK(generated != "layer_1");
    CHECK(scene.find(generated) == nullptr);
}
