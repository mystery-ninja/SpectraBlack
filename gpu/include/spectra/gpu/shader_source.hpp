#pragma once

#include <spectra/core/result.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spectra::gpu {

/// Which dialect the prologue targets.
///
/// Desktop and Android differ in the `#version` line and in whether precision
/// qualifiers are required. That is the entire cross-platform shader story,
/// and it only works because we restricted ourselves to the GL 4.3 ∩ GLES 3.2
/// feature set. See docs/03-RENDER-ENGINE.md.
enum class ShaderLanguage : std::uint8_t {
    DesktopGL43,
    GlesEs32,
};

struct ShaderDefine {
    std::string name;
    std::string value;
};

struct ShaderPreprocessOptions {
    ShaderLanguage language{ShaderLanguage::DesktopGL43};
    std::vector<ShaderDefine> defines;
};

/// Supplies the text behind an `#include`.
///
/// An interface rather than a path, because shaders come from different places
/// depending on who is asking: `render/shaders/` on disk for the editor, a
/// baked-in table for tests, and eventually the APK's asset manager on
/// Android. The preprocessor itself touches no files, which is what keeps it
/// testable without a GL context or a filesystem.
class ShaderIncludeResolver {
public:
    virtual ~ShaderIncludeResolver() = default;

    ShaderIncludeResolver() = default;
    ShaderIncludeResolver(const ShaderIncludeResolver&) = default;
    ShaderIncludeResolver(ShaderIncludeResolver&&) = default;
    ShaderIncludeResolver& operator=(const ShaderIncludeResolver&) = default;
    ShaderIncludeResolver& operator=(ShaderIncludeResolver&&) = default;

    /// Source for `name`, or nothing if this resolver does not have it.
    [[nodiscard]] virtual std::optional<std::string> resolve(std::string_view name) const = 0;
};

/// A resolver backed by a table of strings. For tests, and for shaders that
/// are compiled into the binary.
class MemoryIncludeResolver final : public ShaderIncludeResolver {
public:
    void add(std::string name, std::string source);

    [[nodiscard]] std::optional<std::string> resolve(std::string_view name) const override;

private:
    std::unordered_map<std::string, std::string> sources_;
};

struct PreprocessedShader {
    std::string source;

    /// Index to file name, matching the source-string numbers in the emitted
    /// `#line` directives. `translateLog` turns a driver's "3(42)" or "3:42"
    /// back into "common.glsl:42".
    std::vector<std::string> files;

    /// Rewrite a compiler log so its locations name files instead of indices.
    [[nodiscard]] std::string translateLog(std::string_view log) const;
};

/// Resolve includes and prepend the platform prologue.
///
/// Includes are resolved **once per translation unit**: a file already pulled
/// in is skipped on a second `#include`, the way a header guard would behave.
/// That makes a shared `common.glsl` safe to include from everywhere, and it
/// makes an include cycle terminate rather than recurse.
///
/// The shim owns the `#version` line, so a source file containing one is an
/// error rather than a silent conflict with the prologue.
[[nodiscard]] Result<PreprocessedShader>
preprocessShader(std::string_view name,
                 std::string_view source,
                 const ShaderPreprocessOptions& options = {},
                 const ShaderIncludeResolver* includes = nullptr);

}  // namespace spectra::gpu
