# 04 — Audio Engine

This is the half that After Effects does not have, so it deserves real design
attention rather than being treated as a utility.

## The central constraint

**Audio analysis must be a pure function of time.** Given a track and a timestamp,
the analysis result must be identical whether we are previewing at 60fps,
previewing at 23fps because you have a blur stack running, scrubbing backward, or
exporting offline.

Everything below follows from that one requirement.

## Where Avee gets this wrong (and why it does not matter for Avee)

Avee analyzes audio live, in the render loop, accumulating smoothed state frame
to frame:

```java
// SegmentAudioSpectrumData.AudioProcess — paraphrased
fftMagSmooth[i] = fftMagSmooth[i] * smoothFactor + fftMag[i] * (1 - smoothFactor);
peakBarIndex01Smooth = lerp(peakBarIndex01Smooth, peakBarIndex01, peakBarSmoothFactor);
```

For a live player this is exactly right — it is cheap, it has no latency, and
nobody can tell that a dropped frame changed the decay curve slightly.

For a tool that must export a file matching its preview, it is fatal. The value
at time `t` depends on the entire history of frame timings that led to `t`. Seek
backward and the state is wrong. Drop a frame and preview and export diverge
forever.

## Our model: offline analysis, cached, indexed by time

When a track is loaded, we analyze **the whole thing up front** on worker threads
at a fixed analysis rate, independent of both audio sample rate and project frame
rate.

```
Analysis rate: 200 Hz (5 ms hop)
FFT window:    2048 samples @ 44.1 kHz (46 ms), Hann
Overlap:       ~90%
```

200 Hz is chosen to be comfortably above any project frame rate, so frame values
come from interpolating a denser signal rather than extrapolating a sparser one.
A 4-minute track yields 48,000 analysis frames; at 256 bands of `float16` that is
about 24 MB, which is nothing.

The analysis pass is embarrassingly parallel — split the track into chunks with
window-sized overlap, one chunk per thread, 12 cores finish a 4-minute track in
well under a second.

### What gets cached

```cpp
struct AnalysisFrame {
    float bands[kBandCount];   // smoothed magnitude per band, normalized
    float rms;                 // windowed loudness
    float peak;
    float bassEnergy;          // 20–250 Hz
    float midEnergy;           // 250–4k
    float highEnergy;          // 4k–20k
    float beatEnvelope;        // onset strength, 0..1
    float beatConfidence;
    uint16_t peakBandIndex;
};
```

Smoothing is applied **during** this pass, at the fixed analysis rate, and the
smoothed result is what gets stored. That is the move that makes everything
downstream deterministic: by the time the evaluator asks for a value, all
state-dependent math has already happened at a fixed, reproducible rate.

Querying at an arbitrary time is then a simple interpolation between two cached
frames.

### Cache persistence

Analysis is cached to disk next to the project, keyed by a hash of the audio file
plus the analysis parameters. Reopening a project is instant. Changing analysis
settings invalidates and recomputes.

## DSP pipeline

Worth copying Avee's approach closely here, because it sounds good — the
techniques are well chosen even if the scheduling is wrong for us.

### 1. Window

Hann, matching Avee:

```java
fftWindowMultipliers[i] = DspWindows.hannWindow(i, size) * 0.00390625f;  // 1/256
```

Hann is the right default for visualization: good frequency resolution, mild
spectral leakage, and no scalloping surprises.

### 2. FFT

PFFFT, real-input, 2048-point. Avee uses a Lomont FFT; ours just needs to be
fast and correct. Magnitude per bin:

```
mag[i] = sqrt(re[i]² + im[i]²)
```

### 3. Band mapping

Human hearing is logarithmic, so linear FFT bins are wrong for display — map to
log-spaced or mel-spaced bands. This is why a linear spectrum looks "all bunched
up on the left." Avee offers linear and log; we add mel as the default because it
most closely matches what people perceive as "the bars matching the music."

### 4. Spectral smoothing (across bands)

Avee's `fftSoftness` smooths neighboring bins with a Hamming-weighted kernel:

```java
fftSoftnessWeight[i] = DspWindows.hammingWindow((len + 2) / 2 + i, len + 2);
```

This is what stops the bars looking jittery and uncorrelated. Keep it, exposed
as a "smoothness" parameter.

### 5. Temporal smoothing (across time)

The classic asymmetric envelope: fast attack, slow decay. This is what makes bars
snap up on a transient and fall gracefully, and it is most of what separates a
good-looking visualizer from a twitchy one.

```cpp
float target = bands[i];
float k = (target > smoothed[i]) ? attackCoeff : decayCoeff;
smoothed[i] = smoothed[i] + (target - smoothed[i]) * k;
```

Avee exposes `smoothFactor` and `smoothFactor2`, which appear to be exactly this
attack/decay pair. Because we run at a fixed 200 Hz analysis rate, these
coefficients have a well-defined meaning in seconds rather than "per frame,
whatever that means today."

### 6. Normalization

The hard part, and where most visualizers feel bad. A quiet track should not
produce tiny bars, and a loud master should not clip everything flat.

Approach: a slow-moving automatic gain estimate over a multi-second window, with
a user-controllable floor and ceiling, plus a manual override. Because we analyze
offline we have an advantage Avee cannot have — **we can normalize against the
whole track's statistics**, so the intro is not crushed by the chorus's loudness.
That is a genuinely better result than any realtime player can achieve, and worth
advertising.

### 7. Spectral tilt — the "Avee bass bug"

From the brainstorming:

> *"AE renders audio correct. Avee does render reverbed bass as extreme bass
> which is false and a bug."*

This is a real and well-known failure, and naming the cause tells us what to
build. There are three compounding reasons Avee's bass pins to maximum, and they
need three different fixes.

**Cause 1 — music is not flat, and nobody compensates.** Musical audio has
roughly `1/f` energy distribution ("pink"), so there is genuinely far more energy
at 60 Hz than at 6 kHz. Display raw FFT magnitude and bass *always* dominates —
not because the mix is bass-heavy but because that is what music looks like. This
is the main offender.

The fix is **spectral tilt compensation**: weight each band by roughly `+3 dB per
octave` so that pink noise reads as a flat line. After that, a band only looks
loud when it is loud *relative to what music normally does at that frequency*,
which is what a viewer actually perceives. An optional perceptual (A-weighting or
equal-loudness) curve on top gets closer still.

**Cause 2 — long FFT windows smear reverb in time.** A 2048-sample window at
44.1 kHz spans 46 ms. A reverb tail on a kick stays inside that window for many
consecutive frames, so the bass band keeps reporting energy well after the
transient is over. Low frequencies are the worst case because they need long
windows for frequency resolution — a 40 Hz wave is 25 ms long on its own.

The honest fix is **multi-resolution analysis**: long windows for low bands where
frequency resolution matters, short windows for high bands where timing matters.
A constant-Q transform is the textbook approach. That is Phase 2 work; Phase 1
can ship a single window size and accept some smear.

**Cause 3 — symmetric smoothing holds it up.** With one global decay coefficient,
the bass band's sustained energy never falls. Attack and decay must be
**per-band-group**, so bass can be set to fall faster than it rises while the
highs stay smooth.

The payoff is that our offline analysis makes the correct fix cheap: we can
normalize **per band against that track's own distribution**, so "is this bass
loud?" is answered relative to the rest of the song rather than against an
absolute threshold. No realtime player can do that, and it is precisely the
difference MysterY noticed between AE and Avee.

Expose the tilt as a user control (`Flat` / `Pink` / `Perceptual`) rather than
hardcoding it. "Technically correct" and "looks good on this track" are not
always the same, and Avee's raw look is what its community is used to.

## Beat detection

Needed for `Beat`-family drivers, and good beat detection is a real
differentiator.

**Phase 1 — spectral flux onset detection.** Sum positive frame-to-frame changes
in band magnitude, apply an adaptive median threshold, pick peaks.

```
flux[n] = Σ max(0, mag[n][k] − mag[n−1][k])
onset   = flux[n] > median(flux[n−W .. n+W]) * sensitivity
```

Cheap, reliable for percussive music, and offline analysis lets us use a
*centered* window — looking at future samples, which no realtime detector can do.
That alone makes our onsets noticeably better than Avee's.

**Phase 2 — tempo estimation.** Autocorrelate the onset envelope to find BPM, fit
a beat grid, snap it. Unlocks the genuinely powerful features: snapping keyframes
to beats, and quantizing reactions to musical subdivisions.

**Phase 3 — optional downbeat and section detection**, so "the drop" can be found
automatically.

The beat grid should be **editable**. Automatic detection is 90% correct and the
remaining 10% will drive users insane if they cannot nudge it. Let them drag beat
markers and set tempo manually.

## Reactive signals (the drivers)

Avee's `MeasureDefs` is its public vocabulary of audio reactions. For reference,
what it ships:

```
Constant, Beat, TotalTime, TotalTimeBackward, TotalTimeWhenPlaying,
TotalTimeAndBeat, TrackPosition, BeatRandomShake, BeatCamShakeMore,
BeatCamShakeLess, ConstantShakeMore, ConstantShake, BeatCamShakeRotMore,
BeatCamShakeRotLess, ConstantShakeRotMore, ConstantShakeRotLess,
PeakBarIndex, BeatTriggerAnim, IsPlaying, Nothing
```

This list is instructive but it conflates three different things: raw signals
(`Beat`), time sources (`TrackPosition`), and *pre-baked effects*
(`BeatCamShakeMore`). That last category is why Avee has both `More` and `Less`
variants of everything — the shake amount is baked into the name instead of being
a parameter.

We separate the concerns. A driver is **a signal plus a shaping chain**:

```
Signal          →  Shaping            →  Mapping
─────────────────────────────────────────────────────
Bass energy        attack/decay          range remap
Beat onset         smoothing             curve (ease, exp)
Band[n]            delay                 clamp
RMS                hold                  invert
Peak band index    random per beat
Onset count
```

So Avee's `BeatCamShakeMore` becomes `Beat → random-per-beat → ±0.04 range` and
`BeatCamShakeLess` is the same thing with a smaller range. One concept, a
parameter instead of two names, and the user can build variations Avee's authors
never anticipated.

We will still ship **presets** named after the Avee measures, so an Avee user
finds what they expect and can then open it up and see how it is built.

### Driver vocabulary (Phase 1)

| Signal | Meaning |
|---|---|
| `rms` | Overall loudness |
| `peak` | Peak amplitude |
| `bass` / `mid` / `high` | Band group energy |
| `band(n)` | A single spectrum band |
| `bandRange(a,b)` | Energy across a custom range |
| `beat` | Onset envelope, 0..1 |
| `beatPulse` | Decaying spike per detected beat |
| `beatIndex` | Count of beats elapsed |
| `beatPhase` | Position within the current beat, 0..1 — *the one Avee lacks, and the key to musically-timed motion* |
| `peakBand` | Index of loudest band, normalized |
| `trackPos` | Playhead position, 0..1 |
| `noise(seed)` | Smooth random, not audio-driven but constantly needed |

`beatPhase` is worth highlighting. Because we estimate tempo offline, we can give
a property a value that ramps 0→1 over each beat. That single signal enables
motion locked to the music's grid rather than merely twitching at it, which is
the thing Avee fundamentally cannot do and the thing that will make exports look
professionally timed.

## Playback

miniaudio, with a ring buffer fed by the FFmpeg decoder. The audio thread does
nothing but copy samples — no analysis, no allocation, no locks, no logging.

Scrubbing plays short grains around the playhead. Export does not play audio at
all; it only muxes.

## Latency and sync

Preview must keep visuals aligned with what the speakers are producing. The
output device reports its latency, and we offset the visual clock by it:

```
visualTime = audioClock − deviceLatency + userOffset
```

The `userOffset` exists because Bluetooth headphones can add 150–300 ms that the
device under-reports, and without a manual nudge the preview will look wrong to
users through no fault of ours.

## Testing

Synthetic signals make this genuinely testable, unlike the renderer:

- A 440 Hz sine must put all energy in the expected band.
- A click train at 120 BPM must yield detected beats at exactly 120 BPM.
- Silence must produce zeros, not NaNs. (Normalization dividing by zero energy is
  the single most likely bug in this subsystem.)
- White noise must produce a roughly flat mel spectrum.
- **Determinism:** analyzing the same file twice, including with different thread
  counts, must produce bit-identical output. Chunked parallel analysis with
  overlap is where this would break.
