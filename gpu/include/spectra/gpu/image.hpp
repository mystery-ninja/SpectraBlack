#pragma once

#include <spectra/core/result.hpp>
#include <spectra/gpu/device.hpp>
#include <spectra/gpu/handles.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::gpu {

/// Decoded pixels, always RGBA8 regardless of what the file held.
///
/// The values are sRGB-encoded, because that is what image files contain.
/// `uploadTexture` hands them to the driver as an sRGB texture so that
/// sampling returns linear light and compositing stays correct — converting
/// in a shader instead is the usual way this gets quietly wrong.
struct Image {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;  ///< width * height * 4.

    [[nodiscard]] bool empty() const { return pixels.empty(); }
};

/// Decode PNG, JPEG, BMP, TGA, GIF or PSD from memory.
[[nodiscard]] Result<Image> decodeImage(std::span<const std::byte> bytes);

/// Read and decode a file.
[[nodiscard]] Result<Image> loadImage(std::string_view path);

/// Upload as an sRGB texture, so samples come back in linear light.
[[nodiscard]] Result<TextureHandle> uploadTexture(Device& device,
                                                  const Image& image,
                                                  TextureFilter filter = TextureFilter::Linear,
                                                  bool mipmaps = true);

}  // namespace spectra::gpu
