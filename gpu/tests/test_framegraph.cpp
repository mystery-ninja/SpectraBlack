#include <spectra/gpu/framegraph.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;

namespace {

[[nodiscard]] PassDesc pass(std::string name, std::vector<PassId> inputs = {}) {
    PassDesc desc;
    desc.name = std::move(name);
    desc.width = 64;
    desc.height = 64;
    desc.inputs = std::move(inputs);
    return desc;
}

/// Position of a pass in the compiled order.
[[nodiscard]] std::size_t positionOf(std::span<const PassId> order, PassId id) {
    const auto at = std::find(order.begin(), order.end(), id);
    return static_cast<std::size_t>(std::distance(order.begin(), at));
}

}  // namespace

TEST_CASE("a pass runs after everything it reads") {
    FrameGraph graph;
    const PassId bars = graph.addPass(pass("bars"));
    const PassId background = graph.addPass(pass("background"));
    const PassId composite = graph.addPass(pass("composite", {bars, background}));

    const auto compiled = graph.compile();

    REQUIRE(compiled);
    const auto order = graph.order();
    CHECK(order.size() == 3);
    CHECK(positionOf(order, bars) < positionOf(order, composite));
    CHECK(positionOf(order, background) < positionOf(order, composite));
}

TEST_CASE("a chain comes out in dependency order however it was declared") {
    FrameGraph graph;

    // Declared backwards on purpose: the graph resolves order, the caller
    // does not have to.
    const PassId grade = graph.addPass(pass("grade", {1}));
    const PassId glow = graph.addPass(pass("glow", {2}));
    const PassId visualizer = graph.addPass(pass("visualizer"));

    REQUIRE(graph.compile());
    const auto order = graph.order();

    CHECK(positionOf(order, visualizer) < positionOf(order, glow));
    CHECK(positionOf(order, glow) < positionOf(order, grade));
}

TEST_CASE("the present pass runs last") {
    FrameGraph graph;

    PassDesc screen = pass("screen");
    screen.present = true;
    const PassId present = graph.addPass(std::move(screen));
    const PassId scene = graph.addPass(pass("scene"));

    REQUIRE(graph.compile());
    const auto order = graph.order();

    CHECK(order.back() == present);
    CHECK(positionOf(order, scene) < positionOf(order, present));
}

TEST_CASE("a cycle is reported, naming both passes, instead of hanging") {
    FrameGraph graph;
    graph.addPass(pass("a", {1}));
    graph.addPass(pass("b", {0}));

    const auto compiled = graph.compile();

    REQUIRE_FALSE(compiled);
    CHECK(compiled.error().find("a") != std::string::npos);
    CHECK(compiled.error().find("b") != std::string::npos);
}

TEST_CASE("a pass that reads itself is a cycle too") {
    FrameGraph graph;
    graph.addPass(pass("self", {0}));

    const auto compiled = graph.compile();

    REQUIRE_FALSE(compiled);
    CHECK(compiled.error().find("itself") != std::string::npos);
}

TEST_CASE("an input that does not exist is refused") {
    FrameGraph graph;
    graph.addPass(pass("orphan", {99}));

    const auto compiled = graph.compile();

    REQUIRE_FALSE(compiled);
    CHECK(compiled.error().find("does not exist") != std::string::npos);
}

TEST_CASE("the last reader of a target is the pass that frees it") {
    FrameGraph graph;
    const PassId source = graph.addPass(pass("source"));
    const PassId first = graph.addPass(pass("first", {source}));
    const PassId second = graph.addPass(pass("second", {source}));

    REQUIRE(graph.compile());

    // Whichever of the two readers runs later is the one that releases it.
    const auto order = graph.order();
    const PassId later = positionOf(order, first) > positionOf(order, second) ? first : second;
    CHECK(graph.lastReaderOf(source) == later);
}

TEST_CASE("a pass nothing reads is its own last reader") {
    FrameGraph graph;
    const PassId lonely = graph.addPass(pass("lonely"));

    REQUIRE(graph.compile());

    CHECK(graph.lastReaderOf(lonely) == lonely);
}

TEST_CASE("an empty graph compiles to nothing rather than failing") {
    FrameGraph graph;

    const auto compiled = graph.compile();

    REQUIRE(compiled);
    CHECK(compiled.value().empty());
    CHECK(graph.order().empty());
}

TEST_CASE("clearing lets a graph be rebuilt each frame") {
    FrameGraph graph;
    graph.addPass(pass("one"));
    REQUIRE(graph.compile());
    CHECK(graph.passCount() == 1);

    graph.clear();

    CHECK(graph.passCount() == 0);
    CHECK(graph.order().empty());
}

TEST_CASE("a deep chain does not grow the call stack") {
    FrameGraph graph;

    // Deep enough that a recursive sort on a small thread stack would be in
    // trouble. A nested composition tree is user data, so its depth is not
    // ours to assume.
    constexpr PassId kDepth = 20000;
    graph.addPass(pass("root"));
    for (PassId i = 1; i < kDepth; ++i) {
        graph.addPass(pass("link", {i - 1}));
    }

    REQUIRE(graph.compile());

    const auto order = graph.order();
    REQUIRE(order.size() == kDepth);
    CHECK(order.front() == 0);
    CHECK(order.back() == kDepth - 1);
}
