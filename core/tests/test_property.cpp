#include <spectra/core/property.hpp>
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

Driver constantDriver(float value, BlendOp blend, float amount) {
    Driver driver;
    driver.signal.name = "constant";
    driver.signal.argA = value;
    driver.blend = blend;
    driver.amount = amount;
    return driver;
}

}  // namespace

// The four rows of the table in docs/05-ANIMATION-MODEL.md. These are the
// behaviours the whole project is built to deliver, so they are checked
// directly rather than only through their parts.

TEST_CASE("no keyframes and no drivers is a constant") {
    const StubSignalProvider signals;
    const Property<float> property(3.0f);

    CHECK_FALSE(property.animated());
    CHECK(property.evaluate(0.0, &signals) == doctest::Approx(3.0f));
    CHECK(property.evaluate(99.0, &signals) == doctest::Approx(3.0f));
}

TEST_CASE("keyframes without drivers behave like After Effects") {
    const StubSignalProvider signals;
    Property<float> property(0.0f);
    property.keyframes.insert(key(0.0, 0.0f));
    property.keyframes.insert(key(2.0, 1.0f));

    CHECK(property.animated());
    CHECK(property.evaluate(1.0, &signals) == doctest::Approx(0.5f));
    CHECK(property.evaluate(2.0, &signals) == doctest::Approx(1.0f));
}

TEST_CASE("drivers without keyframes behave like Avee") {
    const StubSignalProvider signals;
    Property<float> property(0.0f);
    property.drivers.push_back(constantDriver(0.8f, BlendOp::Add, 1.0f));

    CHECK(property.animated());
    CHECK(property.evaluate(0.0, &signals) == doctest::Approx(0.8f));
    CHECK(property.evaluate(5.0, &signals) == doctest::Approx(0.8f));
}

TEST_CASE("keyframes and drivers together is the reason this app exists") {
    // A logo scaling in from 0 to 1 over two seconds, with a beat pulse on top.
    const StubSignalProvider signals;
    Property<float> scale(0.0f);
    scale.keyframes.insert(key(0.0, 0.0f));
    scale.keyframes.insert(key(2.0, 1.0f));
    scale.drivers.push_back(constantDriver(1.0f, BlendOp::Multiply, 0.15f));

    // Still exactly invisible at the start: multiply cannot lift it off zero.
    CHECK(scale.evaluate(0.0, &signals) == doctest::Approx(0.0f));

    // Mid-entrance the pulse scales with the animation rather than swamping it.
    CHECK(scale.evaluate(1.0, &signals) == doctest::Approx(0.5f * 1.15f));

    // Fully in, the pulse is the whole of the movement.
    CHECK(scale.evaluate(2.0, &signals) == doctest::Approx(1.15f));
}

TEST_CASE("a null provider evaluates keyframes and skips drivers") {
    Property<float> property(0.0f);
    property.keyframes.insert(key(0.0, 0.0f));
    property.keyframes.insert(key(2.0, 1.0f));
    property.drivers.push_back(constantDriver(1.0f, BlendOp::Add, 10.0f));

    CHECK(property.evaluate(1.0) == doctest::Approx(0.5f));
    CHECK(property.evaluate(1.0, nullptr) == doctest::Approx(0.5f));
}

TEST_CASE("keyframes override the static value once any exist") {
    const StubSignalProvider signals;
    Property<float> property(100.0f);
    property.keyframes.insert(key(0.0, 1.0f));

    CHECK(property.evaluate(0.0, &signals) == doctest::Approx(1.0f));
}

TEST_CASE("drivers stack in order, each on top of the last") {
    const StubSignalProvider signals;
    Property<float> property(1.0f);
    property.drivers.push_back(constantDriver(1.0f, BlendOp::Add, 2.0f));       // 1 -> 3
    property.drivers.push_back(constantDriver(1.0f, BlendOp::Multiply, 1.0f));  // 3 -> 6

    CHECK(property.evaluate(0.0, &signals) == doctest::Approx(6.0f));
}

TEST_CASE("driver order matters and is not silently commuted") {
    const StubSignalProvider signals;

    Property<float> addThenMultiply(1.0f);
    addThenMultiply.drivers.push_back(constantDriver(1.0f, BlendOp::Add, 2.0f));
    addThenMultiply.drivers.push_back(constantDriver(1.0f, BlendOp::Multiply, 1.0f));

    Property<float> multiplyThenAdd(1.0f);
    multiplyThenAdd.drivers.push_back(constantDriver(1.0f, BlendOp::Multiply, 1.0f));
    multiplyThenAdd.drivers.push_back(constantDriver(1.0f, BlendOp::Add, 2.0f));

    CHECK(addThenMultiply.evaluate(0.0, &signals) == doctest::Approx(6.0f));
    CHECK(multiplyThenAdd.evaluate(0.0, &signals) == doctest::Approx(4.0f));
}

TEST_CASE("replace ignores the keyframe curve, which is the Avee escape hatch") {
    const StubSignalProvider signals;
    Property<float> property(0.0f);
    property.keyframes.insert(key(0.0, 0.0f));
    property.keyframes.insert(key(2.0, 1.0f));
    property.drivers.push_back(constantDriver(0.25f, BlendOp::Replace, 1.0f));

    CHECK(property.evaluate(0.0, &signals) == doctest::Approx(0.25f));
    CHECK(property.evaluate(1.0, &signals) == doctest::Approx(0.25f));
    CHECK(property.evaluate(2.0, &signals) == doctest::Approx(0.25f));
}

TEST_CASE("swapping the track leaves keyframes intact and re-derives drivers") {
    // Changing the song must not destroy hand-authored animation. This is the
    // thing After Effects cannot do, because there the audio reaction *is* a
    // set of baked keyframes.
    Property<float> scale(0.0f);
    scale.keyframes.insert(key(0.0, 0.0f));
    scale.keyframes.insert(key(2.0, 1.0f));

    Driver pulse;
    pulse.signal.name = "beatPulse";
    pulse.blend = BlendOp::Multiply;
    pulse.amount = 0.2f;
    scale.drivers.push_back(pulse);

    const StubSignalProvider slowTrack(90.0);
    const StubSignalProvider fastTrack(160.0);

    const float onSlow = scale.evaluate(1.1, &slowTrack);
    const float onFast = scale.evaluate(1.1, &fastTrack);

    // Same keyframe base under both.
    CHECK(scale.base(1.1) == doctest::Approx(0.55f));
    // Different reaction, because the beat grid moved.
    CHECK(onSlow != doctest::Approx(onFast));
}

TEST_CASE("a vector property keeps its keyframe curve under a driver") {
    const StubSignalProvider signals;
    Property<Vec2> position(Vec2{0.0f, 0.0f});

    Keyframe<Vec2> a;
    a.time = 0.0;
    a.value = Vec2{0.0f, 0.0f};
    a.in = a.out = Interpolation::Linear;
    position.keyframes.insert(a);

    Keyframe<Vec2> b;
    b.time = 1.0;
    b.value = Vec2{10.0f, 20.0f};
    b.in = b.out = Interpolation::Linear;
    position.keyframes.insert(b);

    position.drivers.push_back(constantDriver(1.0f, BlendOp::Add, 0.5f));

    const Vec2 value = position.evaluate(0.5, &signals);
    CHECK(value.x == doctest::Approx(5.5f));
    CHECK(value.y == doctest::Approx(10.5f));
}

TEST_CASE("evaluation is repeatable for a whole property") {
    const StubSignalProvider signals;
    Property<float> property(0.0f);
    property.keyframes.insert(key(0.0, 0.0f));
    property.keyframes.insert(key(3.0, 1.0f));

    Driver pulse;
    pulse.signal.name = "bass";
    pulse.blend = BlendOp::Multiply;
    pulse.amount = 0.3f;
    pulse.shaping.envelope.decay = 0.15f;
    pulse.shaping.random.enabled = true;
    pulse.shaping.random.seed = 5;
    property.drivers.push_back(pulse);

    for (int i = 0; i < 90; ++i) {
        const Seconds t = i / 30.0;
        const float first = property.evaluate(t, &signals);
        CHECK(property.evaluate(t, &signals) == doctest::Approx(first));
    }
}
