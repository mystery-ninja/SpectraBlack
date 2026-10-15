# `audio/`

Decoding, playback, and the offline analysis that every audio-reactive property
reads from. Stage 3 of [`docs/15-BUILD-ORDER.md`](../docs/15-BUILD-ORDER.md);
the design it implements is [`docs/04-AUDIO-ENGINE.md`](../docs/04-AUDIO-ENGINE.md).

The one rule that shapes everything here: **analysis is a pure function of
time.** The value of `bass` at 1:04.5 is the same whether you got there by
playing, by scrubbing backward, or by exporting frame 3870 on a virtual clock.
That is why analysis runs offline into a cache rather than accumulating state in
the render loop the way a live player does.

## Layout

| Path | What is in it |
|---|---|
| `include/spectra/audio/` | The public headers. No libav type appears in any of them. |
| `src/` | Everything that does not depend on a particular decoder. |
| `backends/ffmpeg/` | The only translation unit allowed to include `libav*`. |
| `tests/` | doctest cases, run under the `audio.` prefix. |

A second backend — MediaCodec on Android, where shipping FFmpeg is a 20 MB
liability — is a sibling of `backends/ffmpeg/` implementing the same
`decoder.hpp`, selected at configure time.

## Building

FFmpeg is the one dependency we do not fetch and build ourselves. It is
packaged everywhere and compiling it would dwarf the rest of the project.

```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev

cmake --preset linux-audio
cmake --build --preset linux-audio
ctest --preset linux-audio
```

The `ffmpeg` binary is optional and only used by the tests, which transcode a
generated WAV into FLAC, OGG, MP3 and AAC to check the decoder against real
compressed input. Without it those cases report themselves skipped.

## What is here so far

**`buffer.hpp`** — `AudioBuffer`, interleaved float PCM, plus `downmixToMono`.
Reads past the end return silence, because windowed analysis runs off the end
of every track and a bounds check in each caller is a bounds check somebody
forgets.

**`decoder.hpp`** — `probeAudioFile` for what the container claims, and
`decodeAudioFile` for the samples. Whole-file rather than streaming: analysis
wants the whole track anyway, and playback reads from the decoded buffer.

Asking the decoder for one channel gives exactly what `downmixToMono` gives — a
plain average. FFmpeg's own stereo-to-mono matrix is `1/sqrt(2)` per channel,
which is energy-preserving and adds 3 dB to anything panned centre; a correct
choice, but not the same choice, and two definitions of "mono" in one program
is a bug with a long fuse.
