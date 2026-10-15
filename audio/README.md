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

**`fft.hpp`** — `RealFft`, forward only, over PFFFT. The wrapper exists because
PFFFT wants SIMD-aligned buffers, its own scratch, and a packing where the
Nyquist bin rides in the imaginary part of bin zero. One instance per worker
thread; instances are independent.

**`dsp.hpp`** — the primitives, all pure functions: window shapes and their
coherent gains, band layouts in linear/log/mel, FFT-bins-to-bands mapping,
the Hamming smoothing kernel, the asymmetric attack/decay follower, and
spectral tilt.

Three of those are worth singling out, because each fixes something that
visibly goes wrong without it:

- **Bands narrower than a bin interpolate** rather than averaging nothing. At a
  2048-point window the bottom thirty bands of a 128-band log layout are each
  narrower than one FFT bin, and "average the bins inside this band" finds none
  of them. The result is a comb of dead slots at the left of the display, which
  users correctly read as missing bass.
- **Envelope coefficients are given in seconds**, converted against the fixed
  analysis rate. Avee's equivalents are per rendered frame, so the same project
  decays differently on a slower machine.
- **Perceptual tilt is A-weighting clamped at 1 kHz.** The full curve also rolls
  the top octave off by several dB, which is right for a sound level meter and
  wrong for a display — it dims the cymbals. Avee reaches the same conclusion by
  the same clamp in `DspCurves.myAWeight1000`.

**`analysis.hpp`** — `analyzeFile` and the `AnalysisCache` it produces. The
whole track, analysed up front at a fixed 200 Hz into bands plus a per-frame
summary, queryable at any time by interpolation.

### How the pass is split, and why

The obvious parallel decomposition — chunk the track, give each worker a chunk
with window-sized overlap — does not survive the temporal envelope, which is
recursive: each frame's value depends on the frame before it. Overlap makes
the seams *approximately* right, and approximately right is exactly what the
determinism requirement forbids.

So the split follows what is actually pure rather than what is convenient:

1. **Parallel.** Each frame's spectrum, from its own window of samples and
   nothing else. Window, FFT, band mapping, tilt, across-band smoothing. No
   frame reads another, so the work can be divided any way at all.
2. **Serial, whole-track.** The normalization statistics. Cheap, and it is the
   thing no live player can do: the intro is scaled against the song rather
   than against the last two seconds of it.
3. **Serial, in order.** Normalize, run the envelope, measure the flux.
   Recursive, so it runs on one thread — and at roughly 12 million steps for a
   four-minute track, that costs a few hundred milliseconds and buys an answer
   that is bit-identical at any thread count, by construction.

A four-minute track at the default 256 bands takes about 240 ms on twelve
cores in a release build, and 630 ms on one.

Windows are centred on their timestamp rather than starting at it. Starting at
it lags every value by half a window — 23 ms at the defaults, a visible and
entirely avoidable gap between a kick and the bar that reacts to it.

**`onsets.hpp`** — `detectOnsets`, spectral flux peak-picking against a
*centred* adaptive median threshold. Centred is the part that matters and the
part only an offline detector can have: the threshold at 1:04 is allowed to
know what happens at 1:04.25, so the first hit after a quiet passage is not
late the way a realtime detector's always is.

It is a free function over the flux envelope rather than a method on the
cache, because the interesting failures — a click train found at half tempo, a
fade-out found as a drum solo — are far easier to provoke by writing the
envelope directly than by synthesising audio that produces it.

From the detected beats the cache offers `beatIndexAt`, `beatPulseAt` and
`beatPhaseAt`. The last is the one Avee has no equivalent for, and the reason
it cannot do motion locked to the grid rather than merely twitching at it.

**`cache_file.hpp`** — `analyzeFileCached`, and the on-disk format behind it.
Both the audio's content hash and the parameter hash are in the filename, so a
stale cache is never read: it simply is not the file we look for. Flipping a
setting back and forth is therefore free rather than two recomputes.

For a four-minute track the numbers are 237 ms to analyse, 14 ms to write,
27 ms to read, and 48 MB on disk. That is full `float32`, not the `float16`
[`docs/04-AUDIO-ENGINE.md`](../docs/04-AUDIO-ENGINE.md) estimated with:
halving the file would make a loaded cache differ from a fresh analysis, and
then whether preview and export agreed would depend on whether the cache
happened to be warm.

The file is written beside its target and renamed into place, because the
hashes in the name cannot catch a torn write — the name would be right and
only the contents wrong. A cache that cannot be written is not an error; a
project opened from read-only media should cost speed, not function.

**`playback.hpp`** — `Player` over miniaudio, plus the two pieces worth
testing on their own: `visualTime`, which is the latency correction, and
`PlaybackCursor`, which is the playhead.

There is no ring buffer, which departs from the sketch in
[`docs/04-AUDIO-ENGINE.md`](../docs/04-AUDIO-ENGINE.md). That design assumes
streaming, and we decode whole tracks up front for the analysis anyway — so
the buffer is already there, immutable, and the callback reads straight out of
it behind one atomic cursor. A ring buffer would add a producer thread, a fill
policy and an underrun case to solve a problem we do not have.

The cursor is a separate class from the player because it holds all the
behaviour worth testing — wrapping, clamping, running off the end — and the
player needs a sound card to exist at all. CI runners have no sound card, so
the handful of device tests report themselves skipped there while the cursor
tests run everywhere.
