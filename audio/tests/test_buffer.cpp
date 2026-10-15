#include <spectra/audio/buffer.hpp>

#include <doctest/doctest.h>

using namespace spectra;
using namespace spectra::audio;

TEST_CASE("audio buffer reports its shape") {
    const AudioBuffer buffer{
        .samples = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f},
        .sampleRate = 3,
        .channels = 2,
    };

    CHECK(buffer.frameCount() == 3);
    CHECK(buffer.duration() == doctest::Approx(1.0));
    CHECK_FALSE(buffer.empty());
}

TEST_CASE("an empty buffer has no duration and does not divide by zero") {
    const AudioBuffer buffer;

    CHECK(buffer.empty());
    CHECK(buffer.frameCount() == 0);
    CHECK(buffer.duration() == 0.0);
    CHECK(buffer.at(0, 0) == 0.0f);
}

TEST_CASE("reads past the end give silence") {
    const AudioBuffer buffer{
        .samples = {-1.0f, 1.0f},
        .sampleRate = 48000,
        .channels = 2,
    };

    CHECK(buffer.at(0, 0) == -1.0f);
    CHECK(buffer.at(0, 1) == 1.0f);

    // Windowed analysis runs off the end of every track by design, and a
    // bounds check in each caller is a bounds check somebody forgets.
    CHECK(buffer.at(100, 0) == 0.0f);
    CHECK(buffer.at(0, 7) == 0.0f);
    CHECK(buffer.at(0, -1) == 0.0f);
}

TEST_CASE("downmix averages channels") {
    const AudioBuffer stereo{
        .samples = {1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f},
        .sampleRate = 48000,
        .channels = 2,
    };

    const std::vector<float> mono = downmixToMono(stereo);

    REQUIRE(mono.size() == 3);
    CHECK(mono[0] == doctest::Approx(0.5f));
    CHECK(mono[1] == doctest::Approx(0.5f));

    // Out of phase cancels. That is what a mono listener hears, so it is not
    // something to paper over.
    CHECK(mono[2] == doctest::Approx(0.0f));
}

TEST_CASE("downmix of mono is the identity") {
    const AudioBuffer mono{
        .samples = {0.25f, -0.25f, 0.75f},
        .sampleRate = 44100,
        .channels = 1,
    };

    CHECK(downmixToMono(mono) == mono.samples);
}

TEST_CASE("downmix does not scale with channel count") {
    // Summing instead of averaging would make the same material read louder
    // purely for being stored in more channels, and the normalizer downstream
    // has no way to tell the difference.
    AudioBuffer sixChannel{
        .samples = std::vector<float>(6, 0.5f),
        .sampleRate = 48000,
        .channels = 6,
    };

    const std::vector<float> mono = downmixToMono(sixChannel);

    REQUIRE(mono.size() == 1);
    CHECK(mono[0] == doctest::Approx(0.5f));
}
