#include <spectra/audio/playback.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>
#include <vector>

using namespace spectra;
using namespace spectra::audio;

namespace {

/// Frames numbered 1, 2, 3... in every channel, so a read can be checked
/// against where the cursor says it came from.
AudioBuffer countingBuffer(std::size_t frames, int channels = 2) {
    AudioBuffer audio{
        .samples = std::vector<float>(frames * static_cast<std::size_t>(channels)),
        .sampleRate = 48000,
        .channels = channels,
    };
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            audio.samples[frame * static_cast<std::size_t>(channels) +
                          static_cast<std::size_t>(channel)] = static_cast<float>(frame + 1);
        }
    }
    return audio;
}

}  // namespace

// ---------------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------------

TEST_CASE("the visual clock trails the audio clock by the device's latency") {
    CHECK(visualTime(10.0, ClockOffsets{.deviceLatency = 0.02}) == doctest::Approx(9.98));
}

TEST_CASE("the user offset can correct what the device under-reports") {
    // Bluetooth headphones add 150-300 ms that no device reports honestly,
    // and without a manual nudge the preview looks wrong to the user through
    // no fault of ours.
    const ClockOffsets offsets{.deviceLatency = 0.02, .userOffset = -0.2};

    CHECK(visualTime(10.0, offsets) == doctest::Approx(9.78));
    CHECK(visualTime(10.0, ClockOffsets{}) == doctest::Approx(10.0));
}

// ---------------------------------------------------------------------------
// The cursor
// ---------------------------------------------------------------------------

TEST_CASE("a read advances the cursor by what it wrote") {
    const AudioBuffer audio = countingBuffer(100);
    PlaybackCursor cursor(audio.frameCount());

    std::vector<float> out(2 * 16);
    CHECK(cursor.read(audio, out, 16) == 16);
    CHECK(cursor.position() == 16);
    CHECK(out[0] == 1.0f);
    CHECK(out[30] == 16.0f);

    CHECK(cursor.read(audio, out, 16) == 16);
    CHECK(cursor.position() == 32);
    CHECK(out[0] == 17.0f);
}

TEST_CASE("volume scales what is read, not what is stored") {
    const AudioBuffer audio = countingBuffer(10);
    PlaybackCursor cursor(audio.frameCount());

    std::vector<float> out(2 * 4);
    cursor.read(audio, out, 4, 0.5f);

    CHECK(out[0] == doctest::Approx(0.5f));
    CHECK(out[6] == doctest::Approx(2.0f));
    CHECK(audio.samples[0] == 1.0f);
}

TEST_CASE("the end of a track is written as silence, not left alone") {
    // The device's buffer is not guaranteed to arrive zeroed, and whatever is
    // in it is audible.
    const AudioBuffer audio = countingBuffer(10);
    PlaybackCursor cursor(audio.frameCount());
    cursor.seek(8);

    std::vector<float> out(2 * 8, 999.0f);
    const std::size_t written = cursor.read(audio, out, 8);

    CHECK(written == 2);
    CHECK(out[0] == 9.0f);
    CHECK(out[2] == 10.0f);
    for (std::size_t index = 4; index < out.size(); ++index) {
        CAPTURE(index);
        CHECK(out[index] == 0.0f);
    }
    CHECK(cursor.finished());
}

TEST_CASE("looping wraps within a single read") {
    const AudioBuffer audio = countingBuffer(10);
    PlaybackCursor cursor(audio.frameCount());
    cursor.setLooping(true);
    cursor.seek(8);

    std::vector<float> out(2 * 4);
    const std::size_t written = cursor.read(audio, out, 4);

    CHECK(written == 4);
    CHECK(out[0] == 9.0f);
    CHECK(out[2] == 10.0f);
    CHECK(out[4] == 1.0f);
    CHECK(out[6] == 2.0f);
    CHECK(cursor.position() == 2);
    CHECK_FALSE(cursor.finished());
}

TEST_CASE("a loop shorter than the read does not spin forever") {
    const AudioBuffer audio = countingBuffer(3);
    PlaybackCursor cursor(audio.frameCount());
    cursor.setLooping(true);

    std::vector<float> out(2 * 10);
    CHECK(cursor.read(audio, out, 10) == 10);
    CHECK(out[0] == 1.0f);
    CHECK(out[6] == 1.0f);  // wrapped after three frames
}

TEST_CASE("seeking past the end clamps rather than running off it") {
    const AudioBuffer audio = countingBuffer(10);
    PlaybackCursor cursor(audio.frameCount());

    cursor.seek(1000);
    CHECK(cursor.position() == 10);
    CHECK(cursor.finished());

    std::vector<float> out(2 * 4, 1.0f);
    CHECK(cursor.read(audio, out, 4) == 0);
    CHECK(std::all_of(out.begin(), out.end(), [](float value) { return value == 0.0f; }));
}

TEST_CASE("an empty track reads as silence instead of dividing by zero") {
    const AudioBuffer empty;
    PlaybackCursor cursor(0);

    std::vector<float> out(8, 1.0f);
    CHECK(cursor.read(empty, out, 4) == 0);
    CHECK(std::all_of(out.begin(), out.end(), [](float value) { return value == 0.0f; }));

    cursor.setLooping(true);
    CHECK(cursor.read(empty, out, 4) == 0);
}

TEST_CASE("a read never writes past the span it was given") {
    const AudioBuffer audio = countingBuffer(100);
    PlaybackCursor cursor(audio.frameCount());

    // Asked for 32 frames with room for 4. The caller is wrong, and the
    // answer is to write four frames rather than to corrupt the stack.
    std::vector<float> out(2 * 4);
    CHECK(cursor.read(audio, out, 32) == 4);
    CHECK(cursor.position() == 4);
}

TEST_CASE("a mono track plays") {
    const AudioBuffer audio = countingBuffer(10, 1);
    PlaybackCursor cursor(audio.frameCount());

    std::vector<float> out(4);
    CHECK(cursor.read(audio, out, 4) == 4);
    CHECK(out[0] == 1.0f);
    CHECK(out[3] == 4.0f);
}

// ---------------------------------------------------------------------------
// The device
// ---------------------------------------------------------------------------

TEST_CASE("the player opens a device, or says why it cannot") {
    // CI runners have no sound card, so this reports itself skipped there
    // rather than failing. Everything above runs everywhere, which is why
    // the cursor is a separate class.
    auto audio = std::make_shared<AudioBuffer>(countingBuffer(48000));

    const Result<Player> player = Player::create(audio);
    if (!player) {
        MESSAGE("no audio output device: " << player.error());
        return;
    }

    CHECK(player.value().duration() == doctest::Approx(1.0));
    CHECK(player.value().deviceSampleRate() > 0);
    CHECK_FALSE(player.value().playing());

    // Every device buffers something. A reported latency of zero would mean
    // the visual clock is never corrected, which is worse than a rough
    // estimate.
    CHECK(player.value().offsets().deviceLatency > 0.0);
}

TEST_CASE("the player refuses audio it cannot play") {
    CHECK_FALSE(Player::create(nullptr));
    CHECK_FALSE(Player::create(std::make_shared<AudioBuffer>()));
}

TEST_CASE("transport and seeking work without a device being audible") {
    auto audio = std::make_shared<AudioBuffer>(countingBuffer(48000 * 4));

    Result<Player> created = Player::create(audio);
    if (!created) {
        MESSAGE("no audio output device: " << created.error());
        return;
    }
    Player& player = created.value();

    player.seek(2.0);
    CHECK(player.position() == doctest::Approx(2.0));

    player.setUserOffset(-0.1);
    CHECK(player.visualPosition() ==
          doctest::Approx(2.0 - player.offsets().deviceLatency - 0.1).epsilon(0.001));

    player.seek(-5.0);
    CHECK(player.position() == doctest::Approx(0.0));

    player.setVolume(0.25f);
    CHECK(player.volume() == doctest::Approx(0.25f));

    player.play();
    CHECK(player.playing());
    player.pause();
    CHECK_FALSE(player.playing());
}
