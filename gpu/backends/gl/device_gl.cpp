// The OpenGL 4.3 core backend. Everything in this file is the only code in
// the project allowed to make a GL call; see docs/01-ARCHITECTURE.md.

#include <spectra/gpu/device.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spectra::gpu {
namespace {

/// Transparent hash, so a `string_view` uniform name looks up without
/// allocating a `std::string` on every set.
struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view text) const {
        return std::hash<std::string_view>{}(text);
    }
};

using UniformCache = std::unordered_map<std::string, GLint, StringHash, std::equal_to<>>;

struct GlFormat {
    GLenum internalFormat{GL_RGBA8};
    GLenum format{GL_RGBA};
    GLenum type{GL_UNSIGNED_BYTE};
};

[[nodiscard]] GlFormat glFormatFor(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case TextureFormat::SRGB8:
            return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case TextureFormat::RGBA16F:
            return {GL_RGBA16F, GL_RGBA, GL_FLOAT};
        case TextureFormat::R8:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case TextureFormat::R16F:
            return {GL_R16F, GL_RED, GL_FLOAT};
        case TextureFormat::Depth24:
            return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
    }
    return {};
}

[[nodiscard]] GLenum glFilter(TextureFilter filter, bool mipmaps) {
    if (filter == TextureFilter::Nearest) {
        return mipmaps ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
    }
    return mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
}

[[nodiscard]] GLenum glWrap(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case TextureWrap::Repeat:
            return GL_REPEAT;
        case TextureWrap::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

[[nodiscard]] GLenum glBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::SrcColor:
            return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstAlpha:
            return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

[[nodiscard]] GLenum glBufferTarget(BufferType type) {
    switch (type) {
        case BufferType::Vertex:
            return GL_ARRAY_BUFFER;
        case BufferType::Index:
            return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Uniform:
            return GL_UNIFORM_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

[[nodiscard]] GLenum glPrimitive(Primitive primitive) {
    switch (primitive) {
        case Primitive::Triangles:
            return GL_TRIANGLES;
        case Primitive::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case Primitive::Lines:
            return GL_LINES;
    }
    return GL_TRIANGLES;
}

/// A slot table with generation counters. Handing out `index | generation`
/// rather than a raw GL name is what makes a stale handle detectable.
template<typename T>
class SlotTable {
public:
    [[nodiscard]] std::uint32_t insert(T value) {
        if (!free_.empty()) {
            const std::uint32_t index = free_.back();
            free_.pop_back();
            Slot& slot = slots_[index];
            slot.value = std::move(value);
            slot.alive = true;
            return detail::packHandle(index, slot.generation);
        }
        slots_.push_back(Slot{.value = std::move(value), .generation = 1, .alive = true});
        return detail::packHandle(static_cast<std::uint32_t>(slots_.size() - 1), 1);
    }

    [[nodiscard]] T* get(std::uint32_t id) {
        Slot* const slot = slotFor(id);
        return slot != nullptr ? &slot->value : nullptr;
    }

    [[nodiscard]] const T* get(std::uint32_t id) const {
        const Slot* const slot = const_cast<SlotTable*>(this)->slotFor(id);
        return slot != nullptr ? &slot->value : nullptr;
    }

    /// Frees the slot and hands back what was in it, so the caller can delete
    /// the GL object it names.
    [[nodiscard]] bool erase(std::uint32_t id, T& out) {
        Slot* const slot = slotFor(id);
        if (slot == nullptr) {
            return false;
        }
        out = std::move(slot->value);
        slot->alive = false;
        slot->generation = detail::nextGeneration(slot->generation);
        free_.push_back(detail::handleIndex(id));
        return true;
    }

    template<typename Fn>
    void forEach(Fn&& fn) {
        for (Slot& slot : slots_) {
            if (slot.alive) {
                fn(slot.value);
            }
        }
    }

private:
    struct Slot {
        T value{};
        std::uint32_t generation{1};
        bool alive{false};
    };

    [[nodiscard]] Slot* slotFor(std::uint32_t id) {
        if (id == 0) {
            return nullptr;
        }
        const std::uint32_t index = detail::handleIndex(id);
        if (index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[index];
        if (!slot.alive || slot.generation != detail::handleGeneration(id)) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

struct Texture {
    GLuint name{0};
    int width{0};
    int height{0};
    TextureFormat format{TextureFormat::RGBA8};
    bool mipmaps{false};

    /// Owned by a framebuffer, which destroys it. Stops a caller from
    /// deleting a target's colour attachment out from under it.
    bool ownedByFramebuffer{false};
};

struct Buffer {
    GLuint name{0};
    BufferType type{BufferType::Vertex};
    BufferUpdate update{BufferUpdate::Static};
    std::size_t size{0};

    /// Non-null when the buffer is persistently mapped.
    std::byte* mapped{nullptr};
};

struct Shader {
    GLuint program{0};
    UniformCache uniforms;
};

struct Framebuffer {
    GLuint name{0};
    TextureHandle color;
    GLuint depth{0};
    int width{0};
    int height{0};
};

/// Identifies a vertex array object. Two draws sharing buffers and layout
/// share a VAO, which is what keeps steady-state rendering free of GL object
/// creation.
struct VaoKey {
    GLuint vertexBuffer{0};
    GLuint indexBuffer{0};
    std::uint64_t layoutHash{0};

    friend bool operator==(const VaoKey& lhs, const VaoKey& rhs) {
        return lhs.vertexBuffer == rhs.vertexBuffer && lhs.indexBuffer == rhs.indexBuffer &&
               lhs.layoutHash == rhs.layoutHash;
    }
};

struct VaoKeyHash {
    std::size_t operator()(const VaoKey& key) const {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(key.vertexBuffer);
        mix(key.indexBuffer);
        mix(key.layoutHash);
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] std::uint64_t hashLayout(const VertexLayout& layout) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(layout.stride);
    for (const VertexAttribute& attribute : layout.attributes) {
        mix(attribute.location);
        mix(attribute.components);
        mix(static_cast<std::uint64_t>(attribute.type));
        mix(attribute.offset);
        mix(attribute.perInstance ? 1u : 0u);
    }
    return hash;
}

[[nodiscard]] std::string shaderLog(GLuint object, bool isProgram) {
    GLint length = 0;
    if (isProgram) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 1) {
        return {};
    }

    std::string log(static_cast<std::size_t>(length), '\0');
    if (isProgram) {
        glGetProgramInfoLog(object, length, nullptr, log.data());
    } else {
        glGetShaderInfoLog(object, length, nullptr, log.data());
    }
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n')) {
        log.pop_back();
    }
    return log;
}

[[nodiscard]] std::string_view glErrorName(GLenum error) {
    switch (error) {
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            return "GL error";
    }
}

[[nodiscard]] std::string framebufferStatusName(GLenum status) {
    switch (status) {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            return "incomplete attachment";
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            return "no attachments";
        case GL_FRAMEBUFFER_UNSUPPORTED:
            return "format combination unsupported by this driver";
        default:
            return "status 0x" + std::to_string(status);
    }
}

}  // namespace

struct Device::Impl {
    DeviceInfo info;
    FrameStats stats;

    SlotTable<Texture> textures;
    SlotTable<Buffer> buffers;
    SlotTable<Shader> shaders;
    SlotTable<Framebuffer> framebuffers;

    std::unordered_map<VaoKey, GLuint, VaoKeyHash> vaos;

    // Cached state. The device owns these, so it can skip redundant calls and
    // report how many it actually made.
    GLuint boundFramebuffer{0};
    GLuint boundProgram{0};
    BlendState blend{};
    Viewport viewport{};
    bool blendInitialized{false};

    /// Messages from GL_KHR_debug, drained by `takeErrors`.
    std::string debugLog;

    [[nodiscard]] GLint uniformLocation(Shader& shader, std::string_view name) {
        const auto at = shader.uniforms.find(name);
        if (at != shader.uniforms.end()) {
            return at->second;
        }
        const GLint location = glGetUniformLocation(shader.program, std::string(name).c_str());
        shader.uniforms.emplace(name, location);
        return location;
    }

    void bindBlend(const BlendState& state) {
        if (blendInitialized && state == blend) {
            return;
        }
        if (!state.enabled) {
            glDisable(GL_BLEND);
        } else {
            if (!blend.enabled || !blendInitialized) {
                glEnable(GL_BLEND);
            }
            glBlendFuncSeparate(glBlendFactor(state.colorSrc),
                                glBlendFactor(state.colorDst),
                                glBlendFactor(state.alphaSrc),
                                glBlendFactor(state.alphaDst));
            glBlendEquation(GL_FUNC_ADD);
        }
        blend = state;
        blendInitialized = true;
        ++stats.stateChanges;
    }

    void useProgram(GLuint program) {
        if (program == boundProgram) {
            return;
        }
        glUseProgram(program);
        boundProgram = program;
        ++stats.stateChanges;
    }

    [[nodiscard]] GLuint vaoFor(const VaoKey& key,
                                const VertexLayout& layout,
                                const Buffer& vertices,
                                const Buffer* indices) {
        const auto at = vaos.find(key);
        if (at != vaos.end()) {
            return at->second;
        }

        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vertices.name);
        if (indices != nullptr) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->name);
        }

        for (const VertexAttribute& attribute : layout.attributes) {
            glEnableVertexAttribArray(attribute.location);
            const GLenum type =
                attribute.type == AttributeType::Float32 ? GL_FLOAT : GL_UNSIGNED_BYTE;
            const GLboolean normalized =
                attribute.type == AttributeType::UInt8Normalized ? GL_TRUE : GL_FALSE;
            glVertexAttribPointer(
                attribute.location,
                static_cast<GLint>(attribute.components),
                type,
                normalized,
                static_cast<GLsizei>(layout.stride),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attribute.offset)));
            glVertexAttribDivisor(attribute.location, attribute.perInstance ? 1 : 0);
        }

        glBindVertexArray(0);
        vaos.emplace(key, vao);
        ++stats.gpuAllocations;
        return vao;
    }
};

namespace {

/// `userParam` is the device's log string rather than the device itself, so
/// the callback needs no access to private types.
void GLAD_API_PTR debugCallback(GLenum /*source*/,
                                GLenum type,
                                GLuint /*id*/,
                                GLenum severity,
                                GLsizei length,
                                const GLchar* message,
                                const void* userParam) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || userParam == nullptr) {
        return;
    }
    auto& log = *static_cast<std::string*>(const_cast<void*>(userParam));
    if (!log.empty()) {
        log.push_back('\n');
    }
    log.append(type == GL_DEBUG_TYPE_ERROR ? "error: " : "warning: ")
        .append(message, static_cast<std::size_t>(length));
}

}  // namespace

Device::Device() : impl_(std::make_unique<Impl>()) {}

Device::~Device() {
    impl_->framebuffers.forEach([](Framebuffer& framebuffer) {
        glDeleteFramebuffers(1, &framebuffer.name);
        if (framebuffer.depth != 0) {
            glDeleteRenderbuffers(1, &framebuffer.depth);
        }
    });
    impl_->textures.forEach([](Texture& texture) { glDeleteTextures(1, &texture.name); });
    impl_->buffers.forEach([](Buffer& buffer) {
        if (buffer.mapped != nullptr) {
            glBindBuffer(glBufferTarget(buffer.type), buffer.name);
            glUnmapBuffer(glBufferTarget(buffer.type));
        }
        glDeleteBuffers(1, &buffer.name);
    });
    impl_->shaders.forEach([](Shader& shader) { glDeleteProgram(shader.program); });
    for (const auto& [key, vao] : impl_->vaos) {
        glDeleteVertexArrays(1, &vao);
    }
}

Result<std::unique_ptr<Device>> Device::create(ProcAddressLoader loader) {
    using ResultType = Result<std::unique_ptr<Device>>;

    if (loader == nullptr) {
        return ResultType::error("no GL loader supplied");
    }

    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(loader));
    if (version == 0) {
        return ResultType::error("failed to load OpenGL entry points");
    }

    const int major = GLAD_VERSION_MAJOR(version);
    const int minor = GLAD_VERSION_MINOR(version);
    if (major < 4 || (major == 4 && minor < 3)) {
        return ResultType::error("OpenGL 4.3 is required, this context reports " +
                                 std::to_string(major) + "." + std::to_string(minor));
    }

    // Not make_unique: the constructor is private, and keeping it that way is
    // what forces every caller through the version check above.
    std::unique_ptr<Device> device(new Device());
    Impl& impl = *device->impl_;

    const auto glString = [](GLenum name) -> std::string {
        const GLubyte* const text = glGetString(name);
        return text != nullptr ? reinterpret_cast<const char*>(text) : "";
    };
    impl.info.renderer = glString(GL_RENDERER);
    impl.info.vendor = glString(GL_VENDOR);
    impl.info.version = glString(GL_VERSION);
    impl.info.persistentMapping = GLAD_GL_ARB_buffer_storage != 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &impl.info.maxTextureSize);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, &impl.debugLog);

    // Seeding the cached state means the first frame's values are real rather
    // than whatever the context happened to start with.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    impl.bindBlend(blendStateFor(BlendMode::Alpha));

    return ResultType::ok(std::move(device));
}

const DeviceInfo& Device::info() const {
    return impl_->info;
}

// --- Textures --------------------------------------------------------------

Result<TextureHandle> Device::createTexture(const TextureDesc& desc) {
    using ResultType = Result<TextureHandle>;

    if (desc.width <= 0 || desc.height <= 0) {
        return ResultType::error("texture needs a positive size");
    }
    if (desc.width > impl_->info.maxTextureSize || desc.height > impl_->info.maxTextureSize) {
        return ResultType::error("texture exceeds this driver's maximum of " +
                                 std::to_string(impl_->info.maxTextureSize));
    }

    const GlFormat format = glFormatFor(desc.format);

    GLuint name = 0;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 static_cast<GLint>(format.internalFormat),
                 desc.width,
                 desc.height,
                 0,
                 format.format,
                 format.type,
                 desc.pixels);
    glTexParameteri(GL_TEXTURE_2D,
                    GL_TEXTURE_MIN_FILTER,
                    static_cast<GLint>(glFilter(desc.filter, desc.mipmaps)));
    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glFilter(desc.filter, false)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(glWrap(desc.wrap)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(glWrap(desc.wrap)));
    if (desc.mipmaps && desc.pixels != nullptr) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        glDeleteTextures(1, &name);
        return ResultType::error("could not create texture: " + std::string(glErrorName(error)));
    }

    ++impl_->stats.gpuAllocations;
    return ResultType::ok(TextureHandle{impl_->textures.insert(Texture{.name = name,
                                                                       .width = desc.width,
                                                                       .height = desc.height,
                                                                       .format = desc.format,
                                                                       .mipmaps = desc.mipmaps})});
}

void Device::destroyTexture(TextureHandle texture) {
    const Texture* const existing = impl_->textures.get(texture.id);
    if (existing == nullptr || existing->ownedByFramebuffer) {
        return;
    }
    Texture removed;
    if (impl_->textures.erase(texture.id, removed)) {
        glDeleteTextures(1, &removed.name);
    }
}

void Device::updateTexture(TextureHandle texture, const void* pixels) {
    Texture* const existing = impl_->textures.get(texture.id);
    if (existing == nullptr || pixels == nullptr) {
        return;
    }

    const GlFormat format = glFormatFor(existing->format);
    glBindTexture(GL_TEXTURE_2D, existing->name);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    existing->width,
                    existing->height,
                    format.format,
                    format.type,
                    pixels);
    if (existing->mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    impl_->stats.bytesUploaded += static_cast<std::size_t>(existing->width) *
                                  static_cast<std::size_t>(existing->height) *
                                  bytesPerPixel(existing->format);
}

int Device::textureWidth(TextureHandle texture) const {
    const Texture* const existing = impl_->textures.get(texture.id);
    return existing != nullptr ? existing->width : 0;
}

int Device::textureHeight(TextureHandle texture) const {
    const Texture* const existing = impl_->textures.get(texture.id);
    return existing != nullptr ? existing->height : 0;
}

// --- Buffers ---------------------------------------------------------------

Result<BufferHandle> Device::createBuffer(const BufferDesc& desc) {
    using ResultType = Result<BufferHandle>;

    if (desc.size == 0) {
        return ResultType::error("buffer needs a non-zero size");
    }

    const GLenum target = glBufferTarget(desc.type);

    GLuint name = 0;
    glGenBuffers(1, &name);
    glBindBuffer(target, name);

    std::byte* mapped = nullptr;
    if (desc.update == BufferUpdate::Dynamic && impl_->info.persistentMapping) {
        // Mapped once and left mapped. Coherent so that a write is visible to
        // the GPU without an explicit flush, which is what makes per-frame
        // geometry a plain memcpy.
        constexpr GLbitfield kFlags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(
            target, static_cast<GLsizeiptr>(desc.size), desc.data, kFlags | GL_DYNAMIC_STORAGE_BIT);
        mapped = static_cast<std::byte*>(
            glMapBufferRange(target, 0, static_cast<GLsizeiptr>(desc.size), kFlags));
    } else {
        glBufferData(target,
                     static_cast<GLsizeiptr>(desc.size),
                     desc.data,
                     desc.update == BufferUpdate::Dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
    }
    glBindBuffer(target, 0);

    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        glDeleteBuffers(1, &name);
        return ResultType::error("could not create buffer: " + std::string(glErrorName(error)));
    }

    ++impl_->stats.gpuAllocations;
    if (desc.data != nullptr) {
        impl_->stats.bytesUploaded += desc.size;
    }
    return ResultType::ok(BufferHandle{impl_->buffers.insert(Buffer{.name = name,
                                                                    .type = desc.type,
                                                                    .update = desc.update,
                                                                    .size = desc.size,
                                                                    .mapped = mapped})});
}

void Device::destroyBuffer(BufferHandle buffer) {
    Buffer removed;
    if (!impl_->buffers.erase(buffer.id, removed)) {
        return;
    }

    const GLenum target = glBufferTarget(removed.type);
    if (removed.mapped != nullptr) {
        glBindBuffer(target, removed.name);
        glUnmapBuffer(target);
        glBindBuffer(target, 0);
    }
    glDeleteBuffers(1, &removed.name);

    // Any VAO referencing it is now useless, and leaving it would let a
    // recycled GL buffer name resurrect a stale binding.
    for (auto at = impl_->vaos.begin(); at != impl_->vaos.end();) {
        if (at->first.vertexBuffer == removed.name || at->first.indexBuffer == removed.name) {
            glDeleteVertexArrays(1, &at->second);
            at = impl_->vaos.erase(at);
        } else {
            ++at;
        }
    }
}

void Device::updateBuffer(BufferHandle buffer,
                          std::span<const std::byte> data,
                          std::size_t offset) {
    Buffer* const existing = impl_->buffers.get(buffer.id);
    if (existing == nullptr || data.empty()) {
        return;
    }
    if (offset + data.size() > existing->size) {
        return;  // A short write would corrupt geometry silently; drop it.
    }

    if (existing->mapped != nullptr) {
        std::memcpy(existing->mapped + offset, data.data(), data.size());
    } else {
        const GLenum target = glBufferTarget(existing->type);
        glBindBuffer(target, existing->name);
        glBufferSubData(target,
                        static_cast<GLintptr>(offset),
                        static_cast<GLsizeiptr>(data.size()),
                        data.data());
        glBindBuffer(target, 0);
    }
    impl_->stats.bytesUploaded += data.size();
}

std::size_t Device::bufferSize(BufferHandle buffer) const {
    const Buffer* const existing = impl_->buffers.get(buffer.id);
    return existing != nullptr ? existing->size : 0;
}

// --- Shaders ---------------------------------------------------------------

Result<ShaderHandle> Device::createShader(std::string_view vertexSource,
                                          std::string_view fragmentSource,
                                          std::string_view debugName) {
    using ResultType = Result<ShaderHandle>;

    const std::string label = debugName.empty() ? std::string("shader") : std::string(debugName);

    const auto compile =
        [&label](GLenum stage, std::string_view source, std::string& error) -> GLuint {
        const GLuint object = glCreateShader(stage);
        const char* text = source.data();
        const auto length = static_cast<GLint>(source.size());
        glShaderSource(object, 1, &text, &length);
        glCompileShader(object);

        GLint compiled = GL_FALSE;
        glGetShaderiv(object, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) {
            return object;
        }

        error = label + " (" + (stage == GL_VERTEX_SHADER ? "vertex" : "fragment") +
                ") failed to compile:\n" + shaderLog(object, false);
        glDeleteShader(object);
        return 0;
    };

    std::string error;
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, error);
    if (vertex == 0) {
        return ResultType::error(std::move(error));
    }
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return ResultType::error(std::move(error));
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    // Detached and deleted either way: the program holds what it needs.
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        const std::string log = shaderLog(program, true);
        glDeleteProgram(program);
        return ResultType::error(label + " failed to link:\n" + log);
    }

    ++impl_->stats.gpuAllocations;
    return ResultType::ok(ShaderHandle{impl_->shaders.insert(Shader{.program = program})});
}

void Device::destroyShader(ShaderHandle shader) {
    Shader removed;
    if (!impl_->shaders.erase(shader.id, removed)) {
        return;
    }
    if (impl_->boundProgram == removed.program) {
        glUseProgram(0);
        impl_->boundProgram = 0;
    }
    glDeleteProgram(removed.program);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, float value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniform1f(existing->program, impl_->uniformLocation(*existing, name), value);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, int value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniform1i(existing->program, impl_->uniformLocation(*existing, name), value);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, Vec2 value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniform2f(
        existing->program, impl_->uniformLocation(*existing, name), value.x, value.y);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, Vec3 value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniform3f(
        existing->program, impl_->uniformLocation(*existing, name), value.x, value.y, value.z);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, Vec4 value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniform4f(existing->program,
                       impl_->uniformLocation(*existing, name),
                       value.x,
                       value.y,
                       value.z,
                       value.w);
}

void Device::setUniform(ShaderHandle shader, std::string_view name, const Mat4& value) {
    Shader* const existing = impl_->shaders.get(shader.id);
    if (existing == nullptr) {
        return;
    }
    glProgramUniformMatrix4fv(
        existing->program, impl_->uniformLocation(*existing, name), 1, GL_FALSE, &value[0][0]);
}

void Device::setTexture(ShaderHandle shader,
                        std::string_view name,
                        TextureHandle texture,
                        std::uint32_t unit) {
    Shader* const existing = impl_->shaders.get(shader.id);
    const Texture* const image = impl_->textures.get(texture.id);
    if (existing == nullptr || image == nullptr) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, image->name);
    glProgramUniform1i(
        existing->program, impl_->uniformLocation(*existing, name), static_cast<GLint>(unit));
    ++impl_->stats.stateChanges;
}

// --- Framebuffers ----------------------------------------------------------

Result<FramebufferHandle> Device::createFramebuffer(const FramebufferDesc& desc) {
    using ResultType = Result<FramebufferHandle>;

    if (desc.width <= 0 || desc.height <= 0) {
        return ResultType::error("framebuffer needs a positive size");
    }

    Result<TextureHandle> color = createTexture(TextureDesc{.width = desc.width,
                                                            .height = desc.height,
                                                            .format = desc.format,
                                                            .filter = desc.filter,
                                                            .wrap = TextureWrap::ClampToEdge});
    if (!color) {
        return ResultType::error(color.error());
    }

    Texture* const colorTexture = impl_->textures.get(color.value().id);
    colorTexture->ownedByFramebuffer = true;

    GLuint name = 0;
    glGenFramebuffers(1, &name);
    glBindFramebuffer(GL_FRAMEBUFFER, name);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture->name, 0);

    GLuint depth = 0;
    if (desc.depth) {
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, desc.width, desc.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, impl_->boundFramebuffer);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &name);
        if (depth != 0) {
            glDeleteRenderbuffers(1, &depth);
        }
        colorTexture->ownedByFramebuffer = false;
        destroyTexture(color.value());
        return ResultType::error("framebuffer incomplete: " + framebufferStatusName(status));
    }

    ++impl_->stats.gpuAllocations;
    return ResultType::ok(
        FramebufferHandle{impl_->framebuffers.insert(Framebuffer{.name = name,
                                                                 .color = color.value(),
                                                                 .depth = depth,
                                                                 .width = desc.width,
                                                                 .height = desc.height})});
}

void Device::destroyFramebuffer(FramebufferHandle framebuffer) {
    Framebuffer removed;
    if (!impl_->framebuffers.erase(framebuffer.id, removed)) {
        return;
    }

    if (impl_->boundFramebuffer == removed.name) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        impl_->boundFramebuffer = 0;
    }
    glDeleteFramebuffers(1, &removed.name);
    if (removed.depth != 0) {
        glDeleteRenderbuffers(1, &removed.depth);
    }

    if (Texture* const color = impl_->textures.get(removed.color.id); color != nullptr) {
        color->ownedByFramebuffer = false;
    }
    destroyTexture(removed.color);
}

TextureHandle Device::framebufferTexture(FramebufferHandle framebuffer) const {
    const Framebuffer* const existing = impl_->framebuffers.get(framebuffer.id);
    return existing != nullptr ? existing->color : TextureHandle{};
}

int Device::framebufferWidth(FramebufferHandle framebuffer) const {
    const Framebuffer* const existing = impl_->framebuffers.get(framebuffer.id);
    return existing != nullptr ? existing->width : 0;
}

int Device::framebufferHeight(FramebufferHandle framebuffer) const {
    const Framebuffer* const existing = impl_->framebuffers.get(framebuffer.id);
    return existing != nullptr ? existing->height : 0;
}

// --- State and drawing -----------------------------------------------------

void Device::bindFramebuffer(FramebufferHandle framebuffer) {
    const Framebuffer* const existing = impl_->framebuffers.get(framebuffer.id);
    const GLuint name = existing != nullptr ? existing->name : 0;

    if (name != impl_->boundFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, name);
        impl_->boundFramebuffer = name;
        ++impl_->stats.stateChanges;
    }
    if (existing != nullptr) {
        setViewport(Viewport{.x = 0, .y = 0, .width = existing->width, .height = existing->height});
    }
}

void Device::setViewport(const Viewport& viewport) {
    if (viewport == impl_->viewport) {
        return;
    }
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    impl_->viewport = viewport;
    ++impl_->stats.stateChanges;
}

void Device::setBlendMode(BlendMode mode) {
    impl_->bindBlend(blendStateFor(mode));
}

void Device::clear(const Color& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Device::draw(const DrawCall& call) {
    if (call.instanceCount == 0 || call.count == 0) {
        return;
    }

    const Shader* const shader = impl_->shaders.get(call.shader.id);
    Buffer* const vertices = impl_->buffers.get(call.vertices.id);
    if (shader == nullptr || vertices == nullptr || call.layout == nullptr) {
        return;
    }

    Buffer* const indices = call.indices.valid() ? impl_->buffers.get(call.indices.id) : nullptr;
    if (call.indexType != IndexType::None && indices == nullptr) {
        return;
    }

    impl_->useProgram(shader->program);

    const VaoKey key{.vertexBuffer = vertices->name,
                     .indexBuffer = indices != nullptr ? indices->name : 0,
                     .layoutHash = hashLayout(*call.layout)};
    glBindVertexArray(impl_->vaoFor(key, *call.layout, *vertices, indices));

    if (call.indexType == IndexType::None) {
        glDrawArraysInstanced(glPrimitive(call.primitive),
                              static_cast<GLint>(call.first),
                              static_cast<GLsizei>(call.count),
                              static_cast<GLsizei>(call.instanceCount));
    } else {
        const GLenum type =
            call.indexType == IndexType::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        const std::size_t stride = call.indexType == IndexType::UInt16 ? 2 : 4;
        glDrawElementsInstanced(
            glPrimitive(call.primitive),
            static_cast<GLsizei>(call.count),
            type,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(call.first * stride)),
            static_cast<GLsizei>(call.instanceCount));
    }

    glBindVertexArray(0);
    ++impl_->stats.drawCalls;
}

void Device::readPixels(FramebufferHandle framebuffer, std::span<float> out) {
    const Framebuffer* const existing = impl_->framebuffers.get(framebuffer.id);
    if (existing == nullptr) {
        return;
    }

    const auto needed =
        static_cast<std::size_t>(existing->width) * static_cast<std::size_t>(existing->height) * 4;
    if (out.size() != needed) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, existing->name);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, existing->width, existing->height, GL_RGBA, GL_FLOAT, out.data());
    glBindFramebuffer(GL_FRAMEBUFFER, impl_->boundFramebuffer);
}

void Device::beginFrame() {
    impl_->stats = FrameStats{};
}

const FrameStats& Device::stats() const {
    return impl_->stats;
}

std::string Device::takeErrors() {
    std::string messages = std::move(impl_->debugLog);
    impl_->debugLog.clear();

    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        if (!messages.empty()) {
            messages.push_back('\n');
        }
        messages.append(glErrorName(error));
    }
    return messages;
}

}  // namespace spectra::gpu
