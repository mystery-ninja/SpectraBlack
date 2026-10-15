#include <spectra/gpu/image.hpp>

// stb_image reports failures through a return value and `stbi_failure_reason`
// rather than by throwing, which is what we want: this is the only place in
// the module that touches a file, and a bad PNG is a message, not a crash.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <cstdio>
#include <string>
#include <utility>

#include <stb_image.h>

namespace spectra::gpu {
namespace {

constexpr std::size_t kMaxImageBytes = 256u * 1024u * 1024u;

}  // namespace

Result<Image> decodeImage(std::span<const std::byte> bytes) {
    using ResultType = Result<Image>;

    if (bytes.empty()) {
        return ResultType::error("image data is empty");
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // Four channels always, so every path above this has one pixel layout to
    // think about.
    stbi_uc* const decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                   static_cast<int>(bytes.size()),
                                                   &width,
                                                   &height,
                                                   &channels,
                                                   4);
    if (decoded == nullptr) {
        const char* const reason = stbi_failure_reason();
        return ResultType::error(std::string("could not decode image: ") +
                                 (reason != nullptr ? reason : "unknown format"));
    }

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(
        decoded, decoded + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(decoded);

    return ResultType::ok(std::move(image));
}

Result<Image> loadImage(std::string_view path) {
    using ResultType = Result<Image>;

    const std::string name(path);
    std::FILE* const file = std::fopen(name.c_str(), "rb");
    if (file == nullptr) {
        return ResultType::error("could not open " + name);
    }

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size <= 0 || static_cast<std::size_t>(size) > kMaxImageBytes) {
        std::fclose(file);
        return ResultType::error(name + " is empty or implausibly large");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);

    if (read != bytes.size()) {
        return ResultType::error("could not read all of " + name);
    }
    return decodeImage(bytes);
}

Result<TextureHandle>
uploadTexture(Device& device, const Image& image, TextureFilter filter, bool mipmaps) {
    if (image.empty() || image.width <= 0 || image.height <= 0) {
        return Result<TextureHandle>::error("image has no pixels");
    }

    return device.createTexture(TextureDesc{.width = image.width,
                                            .height = image.height,
                                            .format = TextureFormat::SRGB8,
                                            .filter = filter,
                                            .wrap = TextureWrap::ClampToEdge,
                                            .mipmaps = mipmaps,
                                            .pixels = image.pixels.data()});
}

}  // namespace spectra::gpu
