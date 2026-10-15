#include <spectra/core/evalplan.hpp>
#include <spectra/core/scene.hpp>
#include <spectra/core/signal.hpp>

#include <doctest/doctest.h>

#include <ostream>

using namespace spectra;

namespace {

Keyframe<float> key(Seconds time, float value) {
    Keyframe<float> k;
    k.time = time;
    k.value = value;
    k.in = k.out = Interpolation::Linear;
    return k;
}

}  // namespace

TEST_CASE("a plan covers every property of every node in the subtree") {
    CompositionNode root("main");
    root.addChild(std::make_unique<SolidColorNode>("solid"));
    root.addChild(std::make_unique<SpectrumBarsNode>("bars"));

    EvalPlan plan;
    plan.build(root);

    CHECK(plan.nodes().size() == 3);
    CHECK(plan.propertyCount() > 0);
    CHECK(plan.slotOf("solid", "color").has_value());
    CHECK(plan.slotOf("bars", "barCount").has_value());
    CHECK(plan.slotOf("main", "background").has_value());
}

TEST_CASE("nodes come out in draw order, parents before children") {
    CompositionNode root("main");
    Node* const group = root.addChild(std::make_unique<GroupNode>("group"));
    group->addChild(std::make_unique<SolidColorNode>("inner"));
    root.addChild(std::make_unique<SolidColorNode>("after"));

    EvalPlan plan;
    plan.build(root);

    REQUIRE(plan.nodes().size() == 4);
    CHECK(plan.nodes()[0]->id() == "main");
    CHECK(plan.nodes()[1]->id() == "group");
    CHECK(plan.nodes()[2]->id() == "inner");
    CHECK(plan.nodes()[3]->id() == "after");
}

TEST_CASE("slots are distinct and sized by kind") {
    SolidColorNode solid("solid");

    EvalPlan plan;
    plan.build(solid);

    const auto position = plan.slotOf("solid", "position");
    const auto color = plan.slotOf("solid", "color");
    const auto opacity = plan.slotOf("solid", "opacity");

    REQUIRE(position.has_value());
    REQUIRE(color.has_value());
    REQUIRE(opacity.has_value());

    CHECK(position->count == 2);
    CHECK(color->count == 4);
    CHECK(opacity->count == 1);
    CHECK(position->offset != color->offset);
}

TEST_CASE("evaluating writes static values into the output buffer") {
    SolidColorNode solid("solid");
    solid.color.staticValue = Color{0.25f, 0.5f, 0.75f, 1.0f};
    solid.transform.opacity.staticValue = 0.4f;

    EvalPlan plan;
    plan.build(solid);
    plan.evaluate(0.0);

    const Color color = plan.color("solid", "color");
    CHECK(color.r == doctest::Approx(0.25f));
    CHECK(color.g == doctest::Approx(0.5f));
    CHECK(color.b == doctest::Approx(0.75f));
    CHECK(color.a == doctest::Approx(1.0f));
    CHECK(plan.value("solid", "opacity") == doctest::Approx(0.4f));
}

TEST_CASE("the plan evaluates keyframes through the tree") {
    CompositionNode root("main");
    auto bars = std::make_unique<SpectrumBarsNode>("bars");
    bars->transform.opacity.keyframes.insert(key(0.0, 0.0f));
    bars->transform.opacity.keyframes.insert(key(2.0, 1.0f));
    root.addChild(std::move(bars));

    EvalPlan plan;
    plan.build(root);

    plan.evaluate(0.0);
    CHECK(plan.value("bars", "opacity") == doctest::Approx(0.0f));
    plan.evaluate(1.0);
    CHECK(plan.value("bars", "opacity") == doctest::Approx(0.5f));
    plan.evaluate(2.0);
    CHECK(plan.value("bars", "opacity") == doctest::Approx(1.0f));
}

TEST_CASE("the plan applies drivers when a provider is given") {
    SolidColorNode solid("solid");
    solid.transform.opacity.staticValue = 0.5f;

    Driver driver;
    driver.signal.name = "constant";
    driver.signal.argA = 1.0f;
    driver.blend = BlendOp::Add;
    driver.amount = 0.25f;
    solid.transform.opacity.drivers.push_back(driver);

    EvalPlan plan;
    plan.build(solid);

    plan.evaluate(0.0);
    CHECK(plan.value("solid", "opacity") == doctest::Approx(0.5f));

    const StubSignalProvider signals;
    plan.evaluate(0.0, &signals);
    CHECK(plan.value("solid", "opacity") == doctest::Approx(0.75f));
}

TEST_CASE("int and bool properties flatten to one float each") {
    SpectrumBarsNode bars("bars");
    bars.barCount.staticValue = 128;
    bars.scaleMode.staticValue = 2;

    EvalPlan plan;
    plan.build(bars);
    plan.evaluate(0.0);

    CHECK(plan.value("bars", "barCount") == doctest::Approx(128.0f));
    CHECK(plan.value("bars", "scaleMode") == doctest::Approx(2.0f));
}

TEST_CASE("reading a missing slot yields zero instead of taking the frame down") {
    SolidColorNode solid("solid");
    EvalPlan plan;
    plan.build(solid);
    plan.evaluate(0.0);

    CHECK_FALSE(plan.slotOf("solid", "notAProperty").has_value());
    CHECK_FALSE(plan.slotOf("noSuchNode", "color").has_value());
    CHECK(plan.value("noSuchNode", "color") == doctest::Approx(0.0f));
    CHECK(plan.vec2("solid", "notAProperty") == Vec2{0.0f, 0.0f});
}

TEST_CASE("asking for the wrong width gives zero rather than reading past the slot") {
    SolidColorNode solid("solid");
    EvalPlan plan;
    plan.build(solid);
    plan.evaluate(0.0);

    // opacity is one component; asking for four must not run off the end.
    CHECK(plan.color("solid", "opacity") == Color{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(plan.vec2("solid", "opacity") == Vec2{0.0f, 0.0f});
}

TEST_CASE("a node without an id still evaluates, it just cannot be looked up") {
    CompositionNode root("main");
    root.addChild(std::make_unique<SolidColorNode>());  // no id

    EvalPlan plan;
    plan.build(root);
    plan.evaluate(0.0);

    CHECK(plan.nodes().size() == 2);
    CHECK(plan.propertyCount() > 0);
}

TEST_CASE("rebuilding resets the plan rather than appending to it") {
    CompositionNode root("main");
    root.addChild(std::make_unique<SolidColorNode>("a"));

    EvalPlan plan;
    plan.build(root);
    const std::size_t first = plan.propertyCount();

    plan.build(root);
    CHECK(plan.propertyCount() == first);
    CHECK(plan.nodes().size() == 2);
}

TEST_CASE("the plan picks up structural changes after a rebuild") {
    CompositionNode root("main");
    EvalPlan plan;
    plan.build(root);
    CHECK_FALSE(plan.slotOf("late", "color").has_value());

    root.addChild(std::make_unique<SolidColorNode>("late"));
    plan.build(root);
    CHECK(plan.slotOf("late", "color").has_value());
}

TEST_CASE("evaluation is repeatable and order-independent") {
    CompositionNode root("main");
    auto bars = std::make_unique<SpectrumBarsNode>("bars");

    Driver pulse;
    pulse.signal.name = "bass";
    pulse.blend = BlendOp::Multiply;
    pulse.amount = 0.4f;
    pulse.shaping.envelope.decay = 0.2f;
    bars->transform.scale.drivers.push_back(pulse);
    root.addChild(std::move(bars));

    EvalPlan plan;
    plan.build(root);

    const StubSignalProvider signals;

    plan.evaluate(1.37, &signals);
    const Vec2 forward = plan.vec2("bars", "scale");

    plan.evaluate(5.0, &signals);
    plan.evaluate(0.1, &signals);
    plan.evaluate(1.37, &signals);

    CHECK(plan.vec2("bars", "scale").x == doctest::Approx(forward.x));
    CHECK(plan.vec2("bars", "scale").y == doctest::Approx(forward.y));
}
