#include <spectra/gpu/shader_source.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace spectra::gpu {
namespace {

constexpr std::string_view kDesktopPrologue = "#version 430 core\n";

/// ES requires an explicit default precision for float in fragment shaders,
/// and leaving the integer and sampler defaults implicit differs between
/// drivers. Stating all three is the portable form.
constexpr std::string_view kEsPrologue = "#version 320 es\n"
                                         "precision highp float;\n"
                                         "precision highp int;\n"
                                         "precision highp sampler2D;\n";

/// A shader nesting this deep is a mistake, and the limit turns what would be
/// a stack overflow into a message.
constexpr int kMaxIncludeDepth = 32;

[[nodiscard]] std::string_view trimLeft(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    return text.substr(at);
}

/// Indentation is kept — only the trailing whitespace goes, which is what
/// strips the `\r` of a file written on Windows.
[[nodiscard]] std::string_view trimRight(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

/// True when `line` is the preprocessor directive `name`, allowing the spaces
/// that `# include` is also legal with.
[[nodiscard]] bool isDirective(std::string_view line, std::string_view name) {
    const std::string_view body = trimLeft(line);
    if (body.empty() || body.front() != '#') {
        return false;
    }
    const std::string_view rest = trimLeft(body.substr(1));
    if (rest.size() < name.size() || rest.compare(0, name.size(), name) != 0) {
        return false;
    }
    const std::string_view tail = rest.substr(name.size());
    return tail.empty() || tail.front() == ' ' || tail.front() == '\t' || tail.front() == '"' ||
           tail.front() == '<';
}

/// The path out of `#include "foo.glsl"` or `#include <foo.glsl>`.
[[nodiscard]] std::optional<std::string_view> includePath(std::string_view line) {
    const std::size_t open = line.find_first_of("\"<");
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    const char closer = line[open] == '"' ? '"' : '>';
    const std::size_t close = line.find(closer, open + 1);
    if (close == std::string_view::npos || close == open + 1) {
        return std::nullopt;
    }
    return line.substr(open + 1, close - open - 1);
}

[[nodiscard]] std::string location(std::string_view file, int line) {
    return std::string(file) + ":" + std::to_string(line) + ": ";
}

/// Digits to an index, saturating rather than throwing: this runs over driver
/// output, which is not a trusted format.
[[nodiscard]] std::size_t parseIndex(std::string_view digits) {
    constexpr std::size_t kTooLarge = static_cast<std::size_t>(-1);
    std::size_t value = 0;
    for (const char digit : digits) {
        if (value > kTooLarge / 16) {
            return kTooLarge;
        }
        value = value * 10 + static_cast<std::size_t>(digit - '0');
    }
    return value;
}

/// Walks a source file, copying lines through and expanding includes.
class Expander {
public:
    Expander(const ShaderIncludeResolver* includes, PreprocessedShader& out)
        : includes_(includes), out_(out) {}

    [[nodiscard]] bool
    expand(std::string_view file, std::string_view source, int depth, std::string& error) {
        if (depth > kMaxIncludeDepth) {
            error = location(file, 0) + "#include nested more than " +
                    std::to_string(kMaxIncludeDepth) + " deep";
            return false;
        }

        const std::uint32_t index = fileIndex(file);
        emitLineDirective(1, index);

        int lineNumber = 0;
        for (std::size_t at = 0; at <= source.size();) {
            const std::size_t end = std::min(source.find('\n', at), source.size());
            const auto line = source.substr(at, end - at);
            at = end + 1;
            ++lineNumber;

            if (isDirective(line, "version")) {
                error = location(file, lineNumber) +
                        "#version is set by the preprocessor, remove it from the source";
                return false;
            }

            if (!isDirective(line, "include")) {
                out_.source.append(trimRight(line));
                out_.source.push_back('\n');
                if (end == source.size()) {
                    break;
                }
                continue;
            }

            const std::optional<std::string_view> path = includePath(line);
            if (!path) {
                error = location(file, lineNumber) + "malformed #include, expected a quoted path";
                return false;
            }
            if (!expandInclude(*path, file, lineNumber, depth, error)) {
                return false;
            }
            emitLineDirective(lineNumber + 1, index);

            if (end == source.size()) {
                break;
            }
        }
        return true;
    }

private:
    [[nodiscard]] bool expandInclude(std::string_view path,
                                     std::string_view file,
                                     int lineNumber,
                                     int depth,
                                     std::string& error) {
        // Include-once. A second request is not an error: two files sharing a
        // dependency is the normal case, and the first copy already defined
        // everything the second would.
        if (!included_.insert(std::string(path)).second) {
            return true;
        }
        if (includes_ == nullptr) {
            return fail(error, file, lineNumber, path, "no include resolver was supplied");
        }
        const std::optional<std::string> source = includes_->resolve(path);
        if (!source) {
            return fail(error, file, lineNumber, path, "not found");
        }
        return expand(path, *source, depth + 1, error);
    }

    [[nodiscard]] static bool fail(std::string& error,
                                   std::string_view file,
                                   int lineNumber,
                                   std::string_view path,
                                   std::string_view why) {
        error = location(file, lineNumber) + "cannot resolve #include \"" + std::string(path) +
                "\": " + std::string(why);
        return false;
    }

    [[nodiscard]] std::uint32_t fileIndex(std::string_view file) {
        for (std::size_t i = 0; i < out_.files.size(); ++i) {
            if (out_.files[i] == file) {
                return static_cast<std::uint32_t>(i);
            }
        }
        out_.files.emplace_back(file);
        return static_cast<std::uint32_t>(out_.files.size() - 1);
    }

    void emitLineDirective(int lineNumber, std::uint32_t fileIndex) {
        out_.source.append("#line ")
            .append(std::to_string(lineNumber))
            .append(" ")
            .append(std::to_string(fileIndex))
            .push_back('\n');
    }

    const ShaderIncludeResolver* includes_;
    PreprocessedShader& out_;
    std::unordered_set<std::string> included_;
};

}  // namespace

void MemoryIncludeResolver::add(std::string name, std::string source) {
    sources_.insert_or_assign(std::move(name), std::move(source));
}

std::optional<std::string> MemoryIncludeResolver::resolve(std::string_view name) const {
    const auto at = sources_.find(std::string(name));
    if (at == sources_.end()) {
        return std::nullopt;
    }
    return at->second;
}

std::string PreprocessedShader::translateLog(std::string_view log) const {
    std::string out;
    out.reserve(log.size());

    for (std::size_t at = 0; at < log.size();) {
        const bool atTokenStart =
            at == 0 || (std::isalnum(static_cast<unsigned char>(log[at - 1])) == 0 &&
                        log[at - 1] != '_' && log[at - 1] != '.');
        if (!atTokenStart || std::isdigit(static_cast<unsigned char>(log[at])) == 0) {
            out.push_back(log[at]);
            ++at;
            continue;
        }

        std::size_t end = at;
        while (end < log.size() && std::isdigit(static_cast<unsigned char>(log[end])) != 0) {
            ++end;
        }

        // Only a source-string index when a line number follows it, which is
        // what distinguishes "0:42" from an error code or a literal.
        const bool isFileRef = end + 1 < log.size() && (log[end] == ':' || log[end] == '(') &&
                               std::isdigit(static_cast<unsigned char>(log[end + 1])) != 0;
        const std::size_t index = isFileRef ? parseIndex(log.substr(at, end - at)) : files.size();

        if (index < files.size()) {
            out.append(files[index]);
        } else {
            out.append(log.substr(at, end - at));
        }
        at = end;
    }
    return out;
}

Result<PreprocessedShader> preprocessShader(std::string_view name,
                                            std::string_view source,
                                            const ShaderPreprocessOptions& options,
                                            const ShaderIncludeResolver* includes) {
    PreprocessedShader out;
    out.source.append(options.language == ShaderLanguage::GlesEs32 ? kEsPrologue
                                                                   : kDesktopPrologue);
    for (const ShaderDefine& define : options.defines) {
        out.source.append("#define ").append(define.name);
        if (!define.value.empty()) {
            out.source.append(" ").append(define.value);
        }
        out.source.push_back('\n');
    }

    std::string error;
    Expander expander(includes, out);
    if (!expander.expand(name, source, 0, error)) {
        return Result<PreprocessedShader>::error(std::move(error));
    }
    return Result<PreprocessedShader>::ok(std::move(out));
}

}  // namespace spectra::gpu
