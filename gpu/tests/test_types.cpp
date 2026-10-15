#include <spectra/gpu/handles.hpp>
#include <spectra/gpu/types.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <unordered_set>

using namespace spectra::gpu;

TEST_CASE("a default handle is null and compares equal to another null") {
    const TextureHandle a;
    const TextureHandle b;

    CHECK_FALSE(a.valid());
    CHECK_FALSE(static_cast<bool>(a));
    CHECK(a == b);
}

TEST_CASE("handles are usable as keys") {
    std::unordered_set<TextureHandle> set;
    set.insert(TextureHandle{7});
    set.insert(TextureHandle{7});
    set.insert(TextureHandle{9});

    CHECK(set.size() == 2);
    CHECK(set.count(TextureHandle{7}) == 1);
}

TEST_CASE("an index and a generation pack into one id and come back out") {
    const std::uint32_t id = detail::packHandle(12345, 7);

    CHECK(detail::handleIndex(id) == 12345);
    CHECK(detail::handleGeneration(id) == 7);
    CHECK(id != 0);
}

TEST_CASE("generation zero is skipped so slot zero never encodes as null") {
    std::uint32_t generation = 1;
    for (int i = 0; i < 300; ++i) {
        generation = detail::nextGeneration(generation);
        CHECK(generation != 0);
        CHECK(detail::packHandle(0, generation) != 0);
    }
}

TEST_CASE("every blend mode has a name and a distinct equation") {
    const BlendMode modes[] = {BlendMode::None,
                               BlendMode::Alpha,
                               BlendMode::PreMulAlpha,
                               BlendMode::Add,
                               BlendMode::AddAlpha,
                               BlendMode::Screen,
                               BlendMode::Over};

    for (const BlendMode mode : modes) {
        CHECK_FALSE(blendModeName(mode).empty());
    }

    for (std::size_t i = 0; i < std::size(modes); ++i) {
        for (std::size_t j = i + 1; j < std::size(modes); ++j) {
            CHECK_FALSE(blendStateFor(modes[i]) == blendStateFor(modes[j]));
        }
    }
}

TEST_CASE("None disables blending rather than configuring a passthrough") {
    CHECK_FALSE(blendStateFor(BlendMode::None).enabled);
    CHECK(blendStateFor(BlendMode::Alpha).enabled);
}

TEST_CASE("straight and premultiplied alpha differ only in the colour source") {
    const BlendState straight = blendStateFor(BlendMode::Alpha);
    const BlendState premul = blendStateFor(BlendMode::PreMulAlpha);

    CHECK(straight.colorSrc == BlendFactor::SrcAlpha);
    CHECK(premul.colorSrc == BlendFactor::One);
    CHECK(straight.colorDst == premul.colorDst);
    CHECK(straight.alphaSrc == premul.alphaSrc);
    CHECK(straight.alphaDst == premul.alphaDst);
}

TEST_CASE("additive modes never attenuate what is already in the target") {
    CHECK(blendStateFor(BlendMode::Add).colorDst == BlendFactor::One);
    CHECK(blendStateFor(BlendMode::AddAlpha).colorDst == BlendFactor::One);

    // The difference between them is whether source alpha weights the source.
    CHECK(blendStateFor(BlendMode::Add).colorSrc == BlendFactor::One);
    CHECK(blendStateFor(BlendMode::AddAlpha).colorSrc == BlendFactor::SrcAlpha);
}

TEST_CASE("Screen scales the target by the inverse of the source colour") {
    const BlendState screen = blendStateFor(BlendMode::Screen);

    CHECK(screen.colorSrc == BlendFactor::One);
    CHECK(screen.colorDst == BlendFactor::OneMinusSrcColor);
}

TEST_CASE("Over leaves the target's alpha exactly as it found it") {
    const BlendState over = blendStateFor(BlendMode::Over);

    CHECK(over.alphaSrc == BlendFactor::Zero);
    CHECK(over.alphaDst == BlendFactor::One);
}

TEST_CASE("pixel sizes match the formats the pool accounts for") {
    CHECK(bytesPerPixel(TextureFormat::RGBA8) == 4);
    CHECK(bytesPerPixel(TextureFormat::RGBA16F) == 8);
    CHECK(bytesPerPixel(TextureFormat::R8) == 1);
    CHECK(bytesPerPixel(TextureFormat::R16F) == 2);

    CHECK(isDepthFormat(TextureFormat::Depth24));
    CHECK_FALSE(isDepthFormat(TextureFormat::RGBA16F));
}
