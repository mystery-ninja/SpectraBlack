#include <spectra/core/keyframe.hpp>

#include <doctest/doctest.h>

#include <ostream>

using namespace spectra;

namespace {

Keyframe<float> key(Seconds time, float value, Interpolation mode = Interpolation::Linear) {
    Keyframe<float> k;
    k.time = time;
    k.value = value;
    k.in = mode;
    k.out = mode;
    return k;
}

}  // namespace

TEST_CASE("an empty track evaluates to a default value") {
    const KeyframeTrack<float> track;
    CHECK(track.empty());
    CHECK(track.evaluate(0.0) == 0.0f);
    CHECK(track.evaluate(10.0) == 0.0f);
}

TEST_CASE("a single keyframe holds everywhere") {
    KeyframeTrack<float> track;
    track.insert(key(1.0, 5.0f));

    CHECK(track.evaluate(-100.0) == 5.0f);
    CHECK(track.evaluate(1.0) == 5.0f);
    CHECK(track.evaluate(100.0) == 5.0f);
}

TEST_CASE("keyframes stay sorted however they are inserted") {
    KeyframeTrack<float> track;
    track.insert(key(3.0, 30.0f));
    track.insert(key(1.0, 10.0f));
    track.insert(key(2.0, 20.0f));

    REQUIRE(track.size() == 3);
    CHECK(track[0].time == 1.0);
    CHECK(track[1].time == 2.0);
    CHECK(track[2].time == 3.0);
}

TEST_CASE("inserting at an existing time replaces rather than duplicates") {
    KeyframeTrack<float> track;
    track.insert(key(1.0, 10.0f));
    track.insert(key(1.0, 99.0f));

    REQUIRE(track.size() == 1);
    CHECK(track.evaluate(1.0) == 99.0f);
}

TEST_CASE("linear interpolation is exact at the midpoint") {
    KeyframeTrack<float> track;
    track.insert(key(0.0, 0.0f));
    track.insert(key(2.0, 10.0f));

    CHECK(track.evaluate(0.0) == doctest::Approx(0.0f));
    CHECK(track.evaluate(0.5) == doctest::Approx(2.5f));
    CHECK(track.evaluate(1.0) == doctest::Approx(5.0f));
    CHECK(track.evaluate(1.5) == doctest::Approx(7.5f));
    CHECK(track.evaluate(2.0) == doctest::Approx(10.0f));
}

TEST_CASE("the track holds its end values outside the keyframed range") {
    KeyframeTrack<float> track;
    track.insert(key(1.0, 10.0f));
    track.insert(key(2.0, 20.0f));

    CHECK(track.evaluate(0.0) == 10.0f);
    CHECK(track.evaluate(99.0) == 20.0f);
}

TEST_CASE("hold on the outgoing side freezes the segment") {
    KeyframeTrack<float> track;
    track.insert(key(0.0, 0.0f, Interpolation::Hold));
    track.insert(key(1.0, 1.0f, Interpolation::Hold));

    CHECK(track.evaluate(0.0) == 0.0f);
    CHECK(track.evaluate(0.5) == 0.0f);
    CHECK(track.evaluate(0.999) == 0.0f);
    CHECK(track.evaluate(1.0) == 1.0f);
}

TEST_CASE("hold is decided by the left keyframe's outgoing mode alone") {
    KeyframeTrack<float> track;
    Keyframe<float> left = key(0.0, 0.0f, Interpolation::Linear);
    left.out = Interpolation::Hold;
    track.insert(left);
    track.insert(key(1.0, 1.0f, Interpolation::Linear));

    CHECK(track.evaluate(0.5) == 0.0f);
}

TEST_CASE("default bezier handles reproduce linear motion exactly") {
    KeyframeTrack<float> a;
    a.insert(key(0.0, 0.0f, Interpolation::Linear));
    a.insert(key(1.0, 1.0f, Interpolation::Linear));

    KeyframeTrack<float> b;
    b.insert(key(0.0, 0.0f, Interpolation::Bezier));
    b.insert(key(1.0, 1.0f, Interpolation::Bezier));

    for (int i = 0; i <= 10; ++i) {
        const Seconds t = i / 10.0;
        CHECK(a.evaluate(t) == doctest::Approx(b.evaluate(t)));
    }
}

TEST_CASE("an eased segment is symmetric and slower at its edges") {
    KeyframeTrack<float> track;
    track.insert(key(0.0, 0.0f, Interpolation::Ease));
    track.insert(key(1.0, 1.0f, Interpolation::Ease));

    CHECK(track.evaluate(0.5) == doctest::Approx(0.5f).epsilon(0.001));
    CHECK(track.evaluate(0.25) < 0.25f);
    CHECK(track.evaluate(0.75) > 0.75f);

    const float early = track.evaluate(0.5) - track.evaluate(0.4);
    const float edge = track.evaluate(0.1) - track.evaluate(0.0);
    CHECK(early > edge);
}

TEST_CASE("the bezier solver matches independently computed values") {
    // Pinning real numbers, not just shape. Without this, skipping the solve
    // entirely and returning the curve's y at the *time* parameter still looks
    // symmetric, monotonic and correctly slow at the edges — it is a
    // smoothstep, and every shape-only assertion passes. These constants come
    // from an exact-rational bisection done outside this codebase.
    const BezierHandle ease{0.42f, 0.0f};

    CHECK(solveBezier(ease, ease, 0.25f) == doctest::Approx(0.1291619310).epsilon(1e-5));
    CHECK(solveBezier(ease, ease, 0.50f) == doctest::Approx(0.5000000000).epsilon(1e-5));
    CHECK(solveBezier(ease, ease, 0.75f) == doctest::Approx(0.8708380690).epsilon(1e-5));

    // Smoothstep, for contrast: what the above would read if the solve were
    // skipped. Far enough from the real values to be caught.
    CHECK(solveBezier(ease, ease, 0.25f) != doctest::Approx(0.15625).epsilon(1e-3));

    // An asymmetric curve that leaves fast and arrives slow.
    const BezierHandle steep{0.1f, 0.9f};
    CHECK(solveBezier(steep, steep, 0.25f) == doctest::Approx(0.4468223046).epsilon(1e-5));
    CHECK(solveBezier(steep, steep, 0.50f) == doctest::Approx(0.5000000000).epsilon(1e-5));
    CHECK(solveBezier(steep, steep, 0.75f) == doctest::Approx(0.5531776954).epsilon(1e-5));
}

TEST_CASE("the bezier solver is exact at and beyond the segment ends") {
    const BezierHandle ease{0.42f, 0.0f};
    CHECK(solveBezier(ease, ease, 0.0f) == 0.0f);
    CHECK(solveBezier(ease, ease, 1.0f) == 1.0f);
    CHECK(solveBezier(ease, ease, -1.0f) == 0.0f);
    CHECK(solveBezier(ease, ease, 2.0f) == 1.0f);
}

TEST_CASE("an eased curve is monotonic") {
    KeyframeTrack<float> track;
    track.insert(key(0.0, 0.0f, Interpolation::Ease));
    track.insert(key(1.0, 1.0f, Interpolation::Ease));

    float previous = -1.0f;
    for (int i = 0; i <= 200; ++i) {
        const float value = track.evaluate(i / 200.0);
        CHECK(value >= previous);
        previous = value;
    }
}

TEST_CASE("handles with an out-of-range x cannot fold the curve back on itself") {
    KeyframeTrack<float> track;
    Keyframe<float> left = key(0.0, 0.0f, Interpolation::Bezier);
    left.handleOut = BezierHandle{5.0f, 0.0f};
    track.insert(left);
    Keyframe<float> right = key(1.0, 1.0f, Interpolation::Bezier);
    right.handleIn = BezierHandle{5.0f, 0.0f};
    track.insert(right);

    float previous = -1.0f;
    for (int i = 0; i <= 200; ++i) {
        const float value = track.evaluate(i / 200.0);
        CHECK(value >= doctest::Approx(previous).epsilon(0.0001));
        previous = value;
    }
}

TEST_CASE("handle y is free to overshoot so anticipation and bounce work") {
    KeyframeTrack<float> track;
    Keyframe<float> left = key(0.0, 0.0f, Interpolation::Bezier);
    left.handleOut = BezierHandle{0.3f, -0.5f};
    track.insert(left);
    Keyframe<float> right = key(1.0, 1.0f, Interpolation::Bezier);
    right.handleIn = BezierHandle{0.3f, -0.5f};
    track.insert(right);

    bool undershot = false;
    bool overshot = false;
    for (int i = 0; i <= 200; ++i) {
        const float value = track.evaluate(i / 200.0);
        undershot |= value < -0.01f;
        overshot |= value > 1.01f;
    }
    CHECK(undershot);
    CHECK(overshot);
}

TEST_CASE("vector properties interpolate per component") {
    KeyframeTrack<Vec2> track;
    Keyframe<Vec2> a;
    a.time = 0.0;
    a.value = Vec2{0.0f, 10.0f};
    a.in = a.out = Interpolation::Linear;
    track.insert(a);

    Keyframe<Vec2> b;
    b.time = 1.0;
    b.value = Vec2{10.0f, 0.0f};
    b.in = b.out = Interpolation::Linear;
    track.insert(b);

    const Vec2 mid = track.evaluate(0.5);
    CHECK(mid.x == doctest::Approx(5.0f));
    CHECK(mid.y == doctest::Approx(5.0f));
}

TEST_CASE("non-interpolable types hold instead of being averaged") {
    KeyframeTrack<bool> track;
    Keyframe<bool> a;
    a.time = 0.0;
    a.value = false;
    a.in = a.out = Interpolation::Linear;
    track.insert(a);

    Keyframe<bool> b;
    b.time = 1.0;
    b.value = true;
    b.in = b.out = Interpolation::Linear;
    track.insert(b);

    CHECK(track.evaluate(0.5) == false);
    CHECK(track.evaluate(1.0) == true);
}

TEST_CASE("removing a keyframe leaves the rest intact") {
    KeyframeTrack<float> track;
    track.insert(key(0.0, 0.0f));
    track.insert(key(1.0, 1.0f));
    track.insert(key(2.0, 2.0f));

    track.removeAt(1);

    REQUIRE(track.size() == 2);
    CHECK(track[0].time == 0.0);
    CHECK(track[1].time == 2.0);
    CHECK(track.evaluate(1.0) == doctest::Approx(1.0f));
}
