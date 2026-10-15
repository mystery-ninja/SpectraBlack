#include <spectra/gpu/shader_source.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace spectra;
using namespace spectra::gpu;

namespace {

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// Order of first appearance, so a test can assert that one line precedes
/// another without pinning the exact output.
[[nodiscard]] bool
precedes(std::string_view text, std::string_view first, std::string_view second) {
    const std::size_t a = text.find(first);
    const std::size_t b = text.find(second);
    return a != std::string_view::npos && b != std::string_view::npos && a < b;
}

}  // namespace

TEST_CASE("the desktop prologue is a GL 4.3 core version line") {
    const auto result = preprocessShader("test.frag", "void main() {}");

    REQUIRE(result);
    CHECK(result.value().source.starts_with("#version 430 core\n"));
}

TEST_CASE("the ES prologue carries the precision qualifiers desktop does not need") {
    ShaderPreprocessOptions options;
    options.language = ShaderLanguage::GlesEs32;

    const auto result = preprocessShader("test.frag", "void main() {}", options);

    REQUIRE(result);
    CHECK(result.value().source.starts_with("#version 320 es\n"));
    CHECK(contains(result.value().source, "precision highp float;"));
}

TEST_CASE("defines land after the version line, where GLSL requires them") {
    ShaderPreprocessOptions options;
    options.defines = {{"BAR_COUNT", "128"}, {"GLOW", ""}};

    const auto result = preprocessShader("test.frag", "void main() {}", options);

    REQUIRE(result);
    CHECK(contains(result.value().source, "#define BAR_COUNT 128\n"));
    CHECK(contains(result.value().source, "#define GLOW\n"));
    CHECK(precedes(result.value().source, "#version", "#define BAR_COUNT"));
    CHECK(precedes(result.value().source, "#define GLOW", "void main"));
}

TEST_CASE("an include is replaced by its contents") {
    MemoryIncludeResolver includes;
    includes.add("common.glsl", "float luma(vec3 c) { return dot(c, vec3(0.2126)); }");

    const auto result =
        preprocessShader("test.frag", "#include \"common.glsl\"\nvoid main() {}", {}, &includes);

    REQUIRE(result);
    CHECK(contains(result.value().source, "float luma(vec3 c)"));
    CHECK(precedes(result.value().source, "float luma", "void main"));
}

TEST_CASE("angle brackets work the same as quotes") {
    MemoryIncludeResolver includes;
    includes.add("common.glsl", "const float kPi = 3.14159;");

    const auto result = preprocessShader("test.frag", "#include <common.glsl>\n", {}, &includes);

    REQUIRE(result);
    CHECK(contains(result.value().source, "kPi"));
}

TEST_CASE("includes nest") {
    MemoryIncludeResolver includes;
    includes.add("outer.glsl", "#include \"inner.glsl\"\nconst int kOuter = 1;");
    includes.add("inner.glsl", "const int kInner = 2;");

    const auto result = preprocessShader("test.frag", "#include \"outer.glsl\"\n", {}, &includes);

    REQUIRE(result);
    CHECK(precedes(result.value().source, "kInner", "kOuter"));
}

TEST_CASE("a file included twice is expanded once") {
    MemoryIncludeResolver includes;
    includes.add("common.glsl", "const int kShared = 1;");
    includes.add("other.glsl", "#include \"common.glsl\"\nconst int kOther = 2;");

    const auto result = preprocessShader(
        "test.frag", "#include \"common.glsl\"\n#include \"other.glsl\"\n", {}, &includes);

    REQUIRE(result);
    const std::string& source = result.value().source;
    REQUIRE(contains(source, "kShared"));
    CHECK(source.find("kShared") == source.rfind("kShared"));
    CHECK(contains(source, "kOther"));
}

TEST_CASE("an include cycle terminates instead of recursing") {
    MemoryIncludeResolver includes;
    includes.add("a.glsl", "#include \"b.glsl\"\nconst int kA = 1;");
    includes.add("b.glsl", "#include \"a.glsl\"\nconst int kB = 2;");

    const auto result = preprocessShader("test.frag", "#include \"a.glsl\"\n", {}, &includes);

    REQUIRE(result);
    CHECK(contains(result.value().source, "kA"));
    CHECK(contains(result.value().source, "kB"));
}

TEST_CASE("a missing include is an error naming the file and line") {
    const auto result = preprocessShader("test.frag", "\n#include \"nope.glsl\"\n");

    REQUIRE_FALSE(result);
    CHECK(contains(result.error(), "test.frag:2"));
    CHECK(contains(result.error(), "nope.glsl"));
}

TEST_CASE("a malformed include is an error rather than a silently dropped line") {
    const auto result = preprocessShader("test.frag", "#include common.glsl\n");

    REQUIRE_FALSE(result);
    CHECK(contains(result.error(), "malformed"));
}

TEST_CASE("a source file may not set its own #version") {
    const auto result = preprocessShader("test.frag", "#version 330\nvoid main() {}");

    REQUIRE_FALSE(result);
    CHECK(contains(result.error(), "#version"));
}

TEST_CASE("indentation survives, trailing carriage returns do not") {
    const auto result = preprocessShader("test.frag", "void main() {\r\n    int x = 1;\r\n}\r\n");

    REQUIRE(result);
    CHECK(contains(result.value().source, "\n    int x = 1;\n"));
    CHECK_FALSE(contains(result.value().source, "\r"));
}

TEST_CASE("line directives point back at the file a line came from") {
    MemoryIncludeResolver includes;
    includes.add("common.glsl", "const int kShared = 1;");

    const auto result =
        preprocessShader("test.frag", "#include \"common.glsl\"\nvoid main() {}", {}, &includes);

    REQUIRE(result);
    REQUIRE(result.value().files.size() == 2);
    CHECK(result.value().files[0] == "test.frag");
    CHECK(result.value().files[1] == "common.glsl");
    CHECK(contains(result.value().source, "#line 1 1\n"));
    // Back in the root file, on the line after the include.
    CHECK(contains(result.value().source, "#line 2 0\n"));
}

TEST_CASE("a compiler log gets file names in place of source-string indices") {
    MemoryIncludeResolver includes;
    includes.add("common.glsl", "const int kShared = 1;");

    const auto result =
        preprocessShader("bars.frag", "#include \"common.glsl\"\nvoid main() {}", {}, &includes);
    REQUIRE(result);

    // The three shapes the desktop drivers actually emit.
    CHECK(result.value().translateLog("0(12) : error C0000: syntax error") ==
          "bars.frag(12) : error C0000: syntax error");
    CHECK(result.value().translateLog("1:4(10): error: undeclared") ==
          "common.glsl:4(10): error: undeclared");
    CHECK(result.value().translateLog("ERROR: 0:7: 'x' : undeclared identifier") ==
          "ERROR: bars.frag:7: 'x' : undeclared identifier");
}

TEST_CASE("a log with no file reference is left alone") {
    const auto result = preprocessShader("bars.frag", "void main() {}");

    REQUIRE(result);
    CHECK(result.value().translateLog("out of memory") == "out of memory");
    CHECK(result.value().translateLog("error C7011: implicit cast from int to float") ==
          "error C7011: implicit cast from int to float");
}
