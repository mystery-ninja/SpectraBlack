#include <spectra/gpu/image.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;

namespace {

/// A 2x2 uncompressed TGA, built by hand so the test needs no fixture file.
///
/// TGA because its header is 18 bytes of plain fields — a PNG would mean
/// committing a binary blob to test a decoder we did not write.
[[nodiscard]] std::vector<std::byte> tinyTga() {
    std::vector<std::uint8_t> bytes{
        0,  // No image id.
        0,  // No colour map.
        2,  // Uncompressed true-colour.
        0,
        0,
        0,
        0,
        0,  // Colour map spec, unused.
        0,
        0,  // x origin.
        0,
        0,  // y origin.
        2,
        0,  // Width.
        2,
        0,     // Height.
        32,    // Bits per pixel.
        0x28,  // 8 alpha bits, origin at the top-left.
    };

    // Pixels are BGRA in the file: red, green, blue, half-transparent white.
    const std::array<std::array<std::uint8_t, 4>, 4> pixels{
        {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}, {255, 255, 255, 128}}};
    for (const auto& pixel : pixels) {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }

    std::vector<std::byte> out(bytes.size());
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        out[at] = static_cast<std::byte>(bytes[at]);
    }
    return out;
}

}  // namespace

TEST_CASE("a decoded image comes back as RGBA whatever the file held") {
    const auto image = decodeImage(tinyTga());

    REQUIRE(image);
    CHECK(image.value().width == 2);
    CHECK(image.value().height == 2);
    CHECK(image.value().pixels.size() == 2 * 2 * 4);
}

TEST_CASE("channels come out in RGBA order, not the file's BGRA") {
    const auto image = decodeImage(tinyTga());
    REQUIRE(image);
    const auto& pixels = image.value().pixels;

    CHECK(pixels[0] == 255);  // Red pixel.
    CHECK(pixels[1] == 0);
    CHECK(pixels[2] == 0);
    CHECK(pixels[3] == 255);

    CHECK(pixels[4] == 0);  // Green pixel.
    CHECK(pixels[5] == 255);
}

TEST_CASE("alpha survives decoding") {
    const auto image = decodeImage(tinyTga());

    REQUIRE(image);
    CHECK(image.value().pixels[15] == 128);
}

TEST_CASE("garbage reports what the decoder made of it instead of crashing") {
    const std::array<std::byte, 8> junk{};

    const auto image = decodeImage(junk);

    REQUIRE_FALSE(image);
    CHECK(image.error().find("could not decode") != std::string::npos);
}

TEST_CASE("empty input is refused") {
    const auto image = decodeImage({});

    REQUIRE_FALSE(image);
    CHECK_FALSE(image.error().empty());
}

TEST_CASE("a missing file names the path it could not open") {
    const auto image = loadImage("/nonexistent/spectra/logo.png");

    REQUIRE_FALSE(image);
    CHECK(image.error().find("logo.png") != std::string::npos);
}
