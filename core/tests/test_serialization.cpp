#include <spectra/core/json_extras.hpp>
#include <spectra/core/scene.hpp>
#include <spectra/core/serialization.hpp>

#include <doctest/doctest.h>

#include <ostream>

using namespace spectra;

namespace {

std::unique_ptr<Scene> roundTrip(Scene& scene) {
    const std::string text = toJson(scene);
    auto result = fromJson(text);
    REQUIRE_MESSAGE(result.hasValue(), result.error());
    return std::move(result.value());
}

Keyframe<float> key(Seconds time, float value, Interpolation mode = Interpolation::Linear) {
    Keyframe<float> k;
    k.time = time;
    k.value = value;
    k.in = k.out = mode;
    return k;
}

}  // namespace

TEST_CASE("an empty scene round-trips") {
    Scene scene;
    scene.project.name = "Test Project";
    scene.project.width = 2560;
    scene.project.height = 1440;
    scene.project.frameRate = 50.0;
    scene.project.duration = 123.5;

    const auto reloaded = roundTrip(scene);
    CHECK(reloaded->project.name == "Test Project");
    CHECK(reloaded->project.width == 2560);
    CHECK(reloaded->project.height == 1440);
    CHECK(reloaded->project.frameRate == doctest::Approx(50.0));
    CHECK(reloaded->project.duration == doctest::Approx(123.5));
}

TEST_CASE("audio settings round-trip") {
    Scene scene;
    scene.audio.source = "assets/track.mp3";
    scene.audio.analysis.bandCount = 512;
    scene.audio.analysis.bandScale = "log";
    scene.audio.analysis.attack = 0.02f;
    scene.audio.analysis.decay = 0.4f;
    scene.audio.analysis.tilt = "pink";
    scene.audio.beatGrid.bpm = 128.0;
    scene.audio.beatGrid.offset = 0.213;

    const auto reloaded = roundTrip(scene);
    CHECK(reloaded->audio.source == "assets/track.mp3");
    CHECK(reloaded->audio.analysis.bandCount == 512);
    CHECK(reloaded->audio.analysis.bandScale == "log");
    CHECK(reloaded->audio.analysis.attack == doctest::Approx(0.02f));
    CHECK(reloaded->audio.analysis.tilt == "pink");
    CHECK(reloaded->audio.beatGrid.bpm == doctest::Approx(128.0));
    CHECK(reloaded->audio.beatGrid.offset == doctest::Approx(0.213));
}

TEST_CASE("the node tree round-trips with its shape intact") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    main->name = "Main";
    Node* const group = main->addChild(std::make_unique<GroupNode>("viz"));
    group->addChild(std::make_unique<SpectrumBarsNode>("bars"));
    main->addChild(std::make_unique<TextNode>("title"));

    const auto reloaded = roundTrip(scene);

    REQUIRE(reloaded->compositions().size() == 1);
    CHECK(reloaded->compositions()[0]->name == "Main");
    REQUIRE(reloaded->find("viz") != nullptr);
    CHECK(reloaded->find("viz")->children().size() == 1);
    CHECK(reloaded->find("bars")->type() == NodeType::SpectrumBars);
    CHECK(reloaded->find("title")->type() == NodeType::Text);
}

TEST_CASE("node fields round-trip") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto image = std::make_unique<ImageNode>("logo");
    image->name = "Logo";
    image->enabled = false;
    image->blendMode = BlendMode::Screen;
    image->source = "assets/logo.png";
    main->addChild(std::move(image));

    const auto reloaded = roundTrip(scene);
    Node* const node = reloaded->find("logo");
    REQUIRE(node != nullptr);
    CHECK(node->name == "Logo");
    CHECK(node->enabled == false);
    CHECK(node->blendMode == BlendMode::Screen);
    CHECK(static_cast<ImageNode*>(node)->source == "assets/logo.png");
}

TEST_CASE("text content round-trips") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto text = std::make_unique<TextNode>("title");
    text->text = "{Artist} - {Title}";
    text->fontFamily = "Inter";
    main->addChild(std::move(text));

    const auto reloaded = roundTrip(scene);
    const auto* node = static_cast<const TextNode*>(reloaded->find("title"));
    REQUIRE(node != nullptr);
    CHECK(node->text == "{Artist} - {Title}");
    CHECK(node->fontFamily == "Inter");
}

TEST_CASE("static property values round-trip across every kind") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto bars = std::make_unique<SpectrumBarsNode>("bars");

    bars->transform.position.staticValue = Vec2{0.25f, 0.75f};
    bars->transform.rotation.staticValue = 45.0f;
    bars->barCount.staticValue = 192;
    bars->scaleMode.staticValue = 2;
    bars->colorA.staticValue = Color{0.1f, 0.2f, 0.3f, 0.4f};
    main->addChild(std::move(bars));

    const auto reloaded = roundTrip(scene);
    const auto* node = static_cast<const SpectrumBarsNode*>(reloaded->find("bars"));
    REQUIRE(node != nullptr);

    CHECK(node->transform.position.staticValue.x == doctest::Approx(0.25f));
    CHECK(node->transform.position.staticValue.y == doctest::Approx(0.75f));
    CHECK(node->transform.rotation.staticValue == doctest::Approx(45.0f));
    CHECK(node->barCount.staticValue == 192);
    CHECK(node->scaleMode.staticValue == 2);
    CHECK(node->colorA.staticValue.r == doctest::Approx(0.1f));
    CHECK(node->colorA.staticValue.a == doctest::Approx(0.4f));
}

TEST_CASE("keyframes round-trip including handles and interpolation") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto solid = std::make_unique<SolidColorNode>("solid");

    solid->transform.opacity.keyframes.insert(key(0.0, 0.0f, Interpolation::Hold));
    solid->transform.opacity.keyframes.insert(key(1.5, 0.5f, Interpolation::Ease));

    Keyframe<float> shaped = key(3.0, 1.0f, Interpolation::Bezier);
    shaped.handleIn = BezierHandle{0.2f, 0.9f};
    shaped.handleOut = BezierHandle{0.8f, 0.1f};
    solid->transform.opacity.keyframes.insert(shaped);
    main->addChild(std::move(solid));

    const auto reloaded = roundTrip(scene);
    Node* const node = reloaded->find("solid");
    REQUIRE(node != nullptr);

    const auto& track = node->transform.opacity.keyframes;
    REQUIRE(track.size() == 3);
    CHECK(track[0].time == doctest::Approx(0.0));
    CHECK(track[0].out == Interpolation::Hold);
    CHECK(track[1].in == Interpolation::Ease);
    CHECK(track[2].in == Interpolation::Bezier);
    CHECK(track[2].handleIn.x == doctest::Approx(0.2f));
    CHECK(track[2].handleIn.y == doctest::Approx(0.9f));
    CHECK(track[2].handleOut.x == doctest::Approx(0.8f));
}

TEST_CASE("drivers round-trip with their whole shaping chain") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto solid = std::make_unique<SolidColorNode>("solid");

    Driver driver;
    driver.signal.name = "band";
    driver.signal.argA = 3.0f;
    driver.signal.argB = 7.5f;
    driver.blend = BlendOp::Multiply;
    driver.amount = 0.35f;
    driver.enabled = false;
    driver.shaping.remap.inMin = 0.1f;
    driver.shaping.remap.inMax = 0.9f;
    driver.shaping.remap.invert = true;
    driver.shaping.curve.kind = CurveKind::Exponential;
    driver.shaping.curve.exponent = 3.0f;
    driver.shaping.envelope.attack = 0.01f;
    driver.shaping.envelope.decay = 0.25f;
    driver.shaping.delaySeconds = 0.125;
    driver.shaping.random.enabled = true;
    driver.shaping.random.seed = 4242;
    driver.shaping.random.amount = 0.6f;
    solid->transform.scale.drivers.push_back(driver);
    main->addChild(std::move(solid));

    const auto reloaded = roundTrip(scene);
    Node* const node = reloaded->find("solid");
    REQUIRE(node != nullptr);
    REQUIRE(node->transform.scale.drivers.size() == 1);

    const Driver& back = node->transform.scale.drivers[0];
    CHECK(back.signal.name == "band");
    CHECK(back.signal.argA == doctest::Approx(3.0f));
    CHECK(back.signal.argB == doctest::Approx(7.5f));
    CHECK(back.blend == BlendOp::Multiply);
    CHECK(back.amount == doctest::Approx(0.35f));
    CHECK(back.enabled == false);
    CHECK(back.shaping.remap.inMin == doctest::Approx(0.1f));
    CHECK(back.shaping.remap.invert == true);
    CHECK(back.shaping.curve.kind == CurveKind::Exponential);
    CHECK(back.shaping.curve.exponent == doctest::Approx(3.0f));
    CHECK(back.shaping.envelope.attack == doctest::Approx(0.01f));
    CHECK(back.shaping.envelope.decay == doctest::Approx(0.25f));
    CHECK(back.shaping.delaySeconds == doctest::Approx(0.125));
    CHECK(back.shaping.random.enabled == true);
    CHECK(back.shaping.random.seed == 4242);
    CHECK(back.shaping.random.amount == doctest::Approx(0.6f));
}

TEST_CASE("a saved scene reloads to the same JSON a second time") {
    Scene scene;
    Node* const main = scene.addComposition(std::make_unique<CompositionNode>("main"));
    auto bars = std::make_unique<SpectrumBarsNode>("bars");
    bars->transform.opacity.keyframes.insert(key(0.0, 0.0f));
    bars->transform.opacity.keyframes.insert(key(1.0, 1.0f));

    Driver driver;
    driver.signal.name = "bass";
    driver.blend = BlendOp::Add;
    bars->colorA.drivers.push_back(driver);
    main->addChild(std::move(bars));

    const std::string first = toJson(scene);
    auto once = fromJson(first);
    REQUIRE(once.hasValue());
    const std::string second = toJson(*once.value());

    CHECK(first == second);
}

// --- Forward compatibility -------------------------------------------------
//
// The behaviour that is nearly impossible to retrofit once files exist in the
// wild, so it is tested from the first version. See docs/06-PROJECT-FORMAT.md.

TEST_CASE("unknown top-level keys survive a round trip") {
    const std::string input = R"({
        "format": "spectra-black-project",
        "formatVersion": 1,
        "project": { "name": "X" },
        "compositions": [],
        "featureFromTheFuture": { "enabled": true, "weight": 3 }
    })";

    auto loaded = fromJson(input);
    REQUIRE(loaded.hasValue());

    const std::string saved = toJson(*loaded.value());
    auto reloaded = fromJson(saved);
    REQUIRE(reloaded.hasValue());

    REQUIRE(reloaded.value()->extras != nullptr);
    const auto& extras = reloaded.value()->extras->data;
    REQUIRE(extras.contains("featureFromTheFuture"));
    CHECK(extras["featureFromTheFuture"]["enabled"] == true);
    CHECK(extras["featureFromTheFuture"]["weight"] == 3);
}

TEST_CASE("unknown keys on a known node survive a round trip") {
    const std::string input = R"({
        "format": "spectra-black-project",
        "formatVersion": 1,
        "compositions": [{
            "id": "main", "type": "Composition", "children": [{
                "id": "bars", "type": "SpectrumBars",
                "motionBlurSamples": 16,
                "properties": {
                    "barCount": { "static": 99 },
                    "glowRadius": { "static": 0.5 }
                }
            }]
        }]
    })";

    auto loaded = fromJson(input);
    REQUIRE(loaded.hasValue());

    const std::string saved = toJson(*loaded.value());

    // Known values still work.
    auto reloaded = fromJson(saved);
    REQUIRE(reloaded.hasValue());
    const auto* bars = static_cast<const SpectrumBarsNode*>(reloaded.value()->find("bars"));
    REQUIRE(bars != nullptr);
    CHECK(bars->barCount.staticValue == 99);

    // And the parts we never understood are still there.
    CHECK(saved.find("motionBlurSamples") != std::string::npos);
    CHECK(saved.find("glowRadius") != std::string::npos);
}

TEST_CASE("an unknown node type is preserved rather than discarded") {
    const std::string input = R"({
        "format": "spectra-black-project",
        "formatVersion": 1,
        "compositions": [{
            "id": "main", "type": "Composition", "children": [{
                "id": "weird", "type": "VolumetricFogFromV3",
                "name": "Fog",
                "density": 0.8,
                "properties": { "falloff": { "static": 2.5 } }
            }]
        }]
    })";

    auto loaded = fromJson(input);
    REQUIRE(loaded.hasValue());

    Node* const node = loaded.value()->find("weird");
    REQUIRE(node != nullptr);
    CHECK(node->type() == NodeType::Unknown);
    CHECK(static_cast<UnknownNode*>(node)->originalType == "VolumetricFogFromV3");

    const std::string saved = toJson(*loaded.value());
    CHECK(saved.find("VolumetricFogFromV3") != std::string::npos);
    CHECK(saved.find("density") != std::string::npos);
    CHECK(saved.find("falloff") != std::string::npos);
    // Crucially, not written back as a placeholder name.
    CHECK(saved.find("\"type\": \"Unknown\"") == std::string::npos);
}

TEST_CASE("an unknown node keeps its place among its siblings") {
    const std::string input = R"({
        "format": "spectra-black-project",
        "formatVersion": 1,
        "compositions": [{
            "id": "main", "type": "Composition", "children": [
                { "id": "a", "type": "SolidColor" },
                { "id": "b", "type": "SomethingNew" },
                { "id": "c", "type": "SolidColor" }
            ]
        }]
    })";

    auto loaded = fromJson(input);
    REQUIRE(loaded.hasValue());

    const Node* const main = loaded.value()->find("main");
    REQUIRE(main != nullptr);
    REQUIRE(main->children().size() == 3);
    CHECK(main->children()[0]->id() == "a");
    CHECK(main->children()[1]->id() == "b");
    CHECK(main->children()[1]->type() == NodeType::Unknown);
    CHECK(main->children()[2]->id() == "c");
}

// --- Error handling --------------------------------------------------------

TEST_CASE("malformed input reports an error instead of throwing") {
    const auto result = fromJson("{ this is not json");
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().find("JSON") != std::string::npos);
}

TEST_CASE("a non-object root is rejected") {
    const auto result = fromJson("[1, 2, 3]");
    CHECK_FALSE(result.hasValue());
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("another app's JSON is rejected with a useful message") {
    const auto result = fromJson(R"({"format": "some-other-app", "formatVersion": 1})");
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().find("some-other-app") != std::string::npos);
}

TEST_CASE("a project from a newer format version is refused, not half-read") {
    const auto result = fromJson(R"({
        "format": "spectra-black-project",
        "formatVersion": 9999
    })");
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().find("newer version") != std::string::npos);
}

TEST_CASE("missing optional sections fall back to defaults") {
    const auto result = fromJson(R"({
        "format": "spectra-black-project",
        "formatVersion": 1
    })");
    REQUIRE(result.hasValue());
    CHECK(result.value()->project.width == 1920);
    CHECK(result.value()->compositions().empty());
}

TEST_CASE("a property holding the wrong JSON type is ignored, not fatal") {
    const auto result = fromJson(R"({
        "format": "spectra-black-project",
        "formatVersion": 1,
        "compositions": [{
            "id": "main", "type": "Composition", "children": [{
                "id": "bars", "type": "SpectrumBars",
                "properties": { "barCount": { "static": "not a number" } }
            }]
        }]
    })");
    REQUIRE(result.hasValue());
    const auto* bars = static_cast<const SpectrumBarsNode*>(result.value()->find("bars"));
    REQUIRE(bars != nullptr);
    CHECK(bars->barCount.staticValue == 64);  // the default, left alone
}

TEST_CASE("saved JSON is readable and carries its format banner") {
    Scene scene;
    scene.addComposition(std::make_unique<CompositionNode>("main"));

    const std::string text = toJson(scene);
    CHECK(text.find("spectra-black-project") != std::string::npos);
    CHECK(text.find('\n') != std::string::npos);  // indented, so it diffs

    const std::string compact = toJson(scene, -1);
    CHECK(compact.find('\n') == std::string::npos);
}
