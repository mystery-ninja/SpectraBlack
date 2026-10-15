#include <spectra/core/driver.hpp>
#include <spectra/core/signal.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <vector>

using namespace spectra;

namespace {

/// A provider we can hand an exact shape, so envelope and blend behaviour can
/// be checked against hand-computable values rather than the stub's sines.
class StepSignalProvider final : public SignalProvider {
public:
    explicit StepSignalProvider(Seconds edge, float before = 0.0f, float after = 1.0f)
        : edge_(edge), before_(before), after_(after) {}

    [[nodiscard]] float sample(const SignalRef&, Seconds t) const override {
        return t < edge_ ? before_ : after_;
    }

    [[nodiscard]] std::int64_t beatIndex(Seconds t) const override {
        return static_cast<std::int64_t>(std::floor(t));
    }

private:
    Seconds edge_;
    float before_;
    float after_;
};

SignalRef ref(const char* name) {
    SignalRef r;
    r.name = name;
    return r;
}

}  // namespace

TEST_CASE("blend operators match the documented formulas") {
    CHECK(applyBlend(BlendOp::Add, 2.0f, 0.5f, 3.0f) == doctest::Approx(3.5f));
    CHECK(applyBlend(BlendOp::Multiply, 2.0f, 0.5f, 1.0f) == doctest::Approx(3.0f));
    CHECK(applyBlend(BlendOp::Replace, 0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
    CHECK(applyBlend(BlendOp::Max, 2.0f, 0.5f, 1.0f) == doctest::Approx(2.0f));
    CHECK(applyBlend(BlendOp::Min, 2.0f, 0.5f, 1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("multiply keeps a driver from lifting a value off zero") {
    // The scale-entrance case from docs/05-ANIMATION-MODEL.md: a logo animating
    // in from zero must stay invisible at t=0 even with a beat driver on it.
    CHECK(applyBlend(BlendOp::Multiply, 0.0f, 1.0f, 0.15f) == doctest::Approx(0.0f));
    CHECK(applyBlend(BlendOp::Add, 0.0f, 1.0f, 0.15f) == doctest::Approx(0.15f));
}

TEST_CASE("remap rescales and inverts") {
    Remap remap;
    remap.inMin = 0.0f;
    remap.inMax = 1.0f;
    remap.outMin = 10.0f;
    remap.outMax = 20.0f;

    CHECK(remap.apply(0.0f) == doctest::Approx(10.0f));
    CHECK(remap.apply(0.5f) == doctest::Approx(15.0f));
    CHECK(remap.apply(1.0f) == doctest::Approx(20.0f));

    remap.invert = true;
    CHECK(remap.apply(0.0f) == doctest::Approx(20.0f));
    CHECK(remap.apply(1.0f) == doctest::Approx(10.0f));
}

TEST_CASE("a degenerate remap range does not divide by zero") {
    Remap remap;
    remap.inMin = remap.inMax = 1.0f;
    const float v = remap.apply(1.0f);
    CHECK(std::isfinite(v));
}

TEST_CASE("curves behave at their endpoints") {
    Curve curve;
    curve.kind = CurveKind::Linear;
    CHECK(curve.apply(0.3f) == doctest::Approx(0.3f));

    curve.kind = CurveKind::Exponential;
    curve.exponent = 2.0f;
    CHECK(curve.apply(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.apply(1.0f) == doctest::Approx(1.0f));
    CHECK(curve.apply(0.5f) == doctest::Approx(0.25f));

    curve.kind = CurveKind::Smoothstep;
    CHECK(curve.apply(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.apply(0.5f) == doctest::Approx(0.5f));
    CHECK(curve.apply(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("an exponential curve keeps the sign of a bipolar signal") {
    Curve curve;
    curve.kind = CurveKind::Exponential;
    curve.exponent = 2.0f;
    CHECK(curve.apply(-0.5f) == doctest::Approx(-0.25f));
    CHECK(std::isfinite(curve.apply(-0.5f)));
}

TEST_CASE("a driver with no envelope passes the signal straight through") {
    const StubSignalProvider signals;
    Driver driver;
    driver.signal = ref("constant");
    driver.signal.argA = 0.75f;

    CHECK(driver.evaluateSignal(signals, 0.0) == doctest::Approx(0.75f));
}

TEST_CASE("a disabled driver leaves the base value untouched") {
    const StubSignalProvider signals;
    Driver driver;
    driver.signal = ref("constant");
    driver.signal.argA = 1.0f;
    driver.blend = BlendOp::Add;
    driver.enabled = false;

    CHECK(driver.applyTo(5.0f, signals, 0.0) == doctest::Approx(5.0f));
}

TEST_CASE("a driver with no signal name is inert") {
    const StubSignalProvider signals;
    const Driver driver;
    CHECK(driver.applyTo(5.0f, signals, 0.0) == doctest::Approx(5.0f));
}

TEST_CASE("an unknown signal name renders as no reaction rather than failing") {
    const StubSignalProvider signals;
    Driver driver;
    driver.signal = ref("signalFromAFutureVersion");
    driver.blend = BlendOp::Add;

    CHECK(driver.applyTo(5.0f, signals, 0.0) == doctest::Approx(5.0f));
}

TEST_CASE("a driver applies to every component of a vector property") {
    const StubSignalProvider signals;
    Driver driver;
    driver.signal = ref("constant");
    driver.signal.argA = 1.0f;
    driver.blend = BlendOp::Add;
    driver.amount = 2.0f;

    const Vec2 result = driver.applyTo(Vec2{1.0f, 10.0f}, signals, 0.0);
    CHECK(result.x == doctest::Approx(3.0f));
    CHECK(result.y == doctest::Approx(12.0f));
}

TEST_CASE("the envelope lags a step instead of following it instantly") {
    const StepSignalProvider signals(1.0);
    ShapingChain chain;
    chain.envelope.attack = 0.1f;
    chain.envelope.decay = 0.1f;

    const float before = chain.apply(signals, ref("x"), 0.9);
    const float just = chain.apply(signals, ref("x"), 1.01);
    const float later = chain.apply(signals, ref("x"), 1.5);

    CHECK(before == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(just < 0.5f);
    CHECK(later > 0.98f);
    CHECK(just > before);
}

TEST_CASE("attack and decay can differ") {
    const StepSignalProvider signals(1.0);

    ShapingChain fast;
    fast.envelope.attack = 0.01f;
    fast.envelope.decay = 1.0f;

    ShapingChain slow;
    slow.envelope.attack = 1.0f;
    slow.envelope.decay = 0.01f;

    CHECK(fast.apply(signals, ref("x"), 1.05) > slow.apply(signals, ref("x"), 1.05));
}

TEST_CASE("the envelope is a pure function of time, not of evaluation order") {
    // The property that makes preview and export agree. Walking time forwards,
    // backwards, or jumping around must give identical values.
    const StubSignalProvider signals;
    ShapingChain chain;
    chain.envelope.attack = 0.05f;
    chain.envelope.decay = 0.2f;

    std::vector<float> forward;
    for (int i = 0; i < 60; ++i) {
        forward.push_back(chain.apply(signals, ref("bass"), i / 60.0));
    }

    for (int i = 59; i >= 0; --i) {
        CHECK(chain.apply(signals, ref("bass"), i / 60.0) ==
              doctest::Approx(forward[static_cast<std::size_t>(i)]));
    }

    CHECK(chain.apply(signals, ref("bass"), 30 / 60.0) == doctest::Approx(forward[30]));
    CHECK(chain.apply(signals, ref("bass"), 7 / 60.0) == doctest::Approx(forward[7]));
}

TEST_CASE("the envelope agrees across frame rates where the grids coincide") {
    // Preview runs at whatever rate the machine manages; export runs on a
    // virtual clock. Walking 24 fps and 60 fps has to land on the same values
    // at the times both grids share, or the two disagree on the same frame.
    const StubSignalProvider signals;
    ShapingChain chain;
    chain.envelope.attack = 0.05f;
    chain.envelope.decay = 0.2f;

    std::vector<float> at24;
    for (int frame = 0; frame <= 48; ++frame) {
        at24.push_back(chain.apply(signals, ref("bass"), frame / 24.0));
    }

    std::vector<float> at60;
    for (int frame = 0; frame <= 120; ++frame) {
        at60.push_back(chain.apply(signals, ref("bass"), frame / 60.0));
    }

    // Every whole second is on both grids.
    for (int second = 0; second <= 2; ++second) {
        CHECK(at24[static_cast<std::size_t>(second * 24)] ==
              doctest::Approx(at60[static_cast<std::size_t>(second * 60)]));
    }
}

TEST_CASE("delay shifts the signal in time") {
    const StepSignalProvider signals(1.0);
    ShapingChain chain;
    chain.delaySeconds = 0.5;

    CHECK(chain.apply(signals, ref("x"), 1.2) == doctest::Approx(0.0f));
    CHECK(chain.apply(signals, ref("x"), 1.6) == doctest::Approx(1.0f));
}

TEST_CASE("the random stage is stable within a beat and changes across beats") {
    const StubSignalProvider signals(120.0);
    ShapingChain chain;
    chain.random.enabled = true;
    chain.random.seed = 42;
    chain.remap.inMin = 0.0f;
    chain.remap.inMax = 1.0f;

    SignalRef constant = ref("constant");
    constant.argA = 1.0f;

    // 120 BPM is a beat every 0.5 s.
    const float early = chain.apply(signals, constant, 0.1);
    const float late = chain.apply(signals, constant, 0.4);
    const float next = chain.apply(signals, constant, 0.6);

    CHECK(early == doctest::Approx(late));
    CHECK(early != doctest::Approx(next));
}

TEST_CASE("the random stage repeats when scrubbed back") {
    const StubSignalProvider signals(120.0);
    ShapingChain chain;
    chain.random.enabled = true;
    chain.random.seed = 7;

    SignalRef constant = ref("constant");
    constant.argA = 1.0f;

    const float first = chain.apply(signals, constant, 3.2);
    for (int i = 0; i < 20; ++i) {
        (void)chain.apply(signals, constant, i * 0.37);
    }
    CHECK(chain.apply(signals, constant, 3.2) == doctest::Approx(first));
}

TEST_CASE("different seeds give different random sequences") {
    const StubSignalProvider signals(120.0);
    SignalRef constant = ref("constant");
    constant.argA = 1.0f;

    ShapingChain a;
    a.random.enabled = true;
    a.random.seed = 1;

    ShapingChain b;
    b.random.enabled = true;
    b.random.seed = 2;

    int differences = 0;
    for (int beat = 0; beat < 16; ++beat) {
        const Seconds t = beat * 0.5 + 0.1;
        if (a.apply(signals, constant, t) != doctest::Approx(b.apply(signals, constant, t))) {
            ++differences;
        }
    }
    CHECK(differences > 12);
}

TEST_CASE("a random amount of zero is a no-op") {
    const StubSignalProvider signals;
    ShapingChain chain;
    chain.random.enabled = true;
    chain.random.seed = 99;
    chain.random.amount = 0.0f;

    SignalRef constant = ref("constant");
    constant.argA = 0.8f;

    CHECK(chain.apply(signals, constant, 1.3) == doctest::Approx(0.8f));
}

TEST_CASE("the stub provider is deterministic and bounded") {
    const StubSignalProvider signals;
    for (const char* name : {"bass", "mid", "treble", "level", "beatPulse"}) {
        for (int i = 0; i < 100; ++i) {
            const Seconds t = i * 0.037;
            const float v = signals.sample(ref(name), t);
            CHECK(v >= 0.0f);
            CHECK(v <= 1.0f);
            CHECK(v == doctest::Approx(signals.sample(ref(name), t)));
        }
    }
}

TEST_CASE("the stub beat index advances once per beat") {
    const StubSignalProvider signals(120.0);
    CHECK(signals.beatIndex(0.0) == 0);
    CHECK(signals.beatIndex(0.49) == 0);
    CHECK(signals.beatIndex(0.51) == 1);
    CHECK(signals.beatIndex(-0.1) == -1);
}
