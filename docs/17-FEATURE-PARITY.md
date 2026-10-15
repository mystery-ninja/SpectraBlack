# 17 — Feature Parity Ledger

**The rule: nothing from either parent app disappears by accident.**

Spectra Black exists to combine After Effects and Avee Player. A feature from
either one may only be left out as a **written decision with a reason**, never
by being forgotten, overlooked, or quietly dropped because it looked hard.

This document is the mechanism that makes that rule real. A principle stated in
the vision doc is a hope; a checked inventory is a guarantee.

## How to use this

Every row is a capability from one of the parent apps. The status says where it
stands:

| Status | Meaning |
|---|---|
| **Done** | Built and tested in this repo |
| **Building** | Someone is on it now |
| **Planned** | Agreed, scheduled, not started |
| **Deferred** | We want it, but not yet — the phase says when |
| **Dropped** | We are deliberately not doing it. **A reason is mandatory.** |

> **If you are removing something from the plan, you may not delete its row.**
> Change the status to Dropped and write why. A future contributor asking "why
> doesn't Spectra Black have X?" deserves an answer better than silence.
>
> Likewise, if you find a feature in either parent that is not listed here, add
> it. An incomplete ledger is worse than none, because it looks complete.

Phases refer to [`08-ROADMAP.md`](08-ROADMAP.md). Stages refer to
[`15-BUILD-ORDER.md`](15-BUILD-ORDER.md).

---

## Avee Player — element types

The authoritative list, read from `ElementsFactory` in the **smali** output of
Avee RE 1.2.101. This matters: `java_src/` in the reference decompile is
incomplete and contains only 15 of these, missing every effect the RE fork
added. Anyone checking our coverage against `java_src/` will conclude we are
done when we are not.

Twenty-six real types, plus `Root` (the tree root) and `unk`.

| Avee type | Spectra Black | Status | Notes |
|---|---|---|---|
| `Bars` | `SpectrumBars` | **Planned** | Stage 4. The signature element of the whole app |
| `AudioProvider` | `AudioSource` | **Planned** | Phase 1. Bound by explicit reference, not layer order — see [`14`](14-VIZ-COMPATIBILITY.md) |
| `Composition` | `Composition` | **Planned** | Stage 1 node tree, nesting in Phase 2 |
| `Image` | `Image` | **Planned** | Stage 4 |
| `Text` | `Text` | **Planned** | Basic atlas Phase 1, FreeType + HarfBuzz Phase 2 |
| `StatText` | `Text` with fields | **Planned** | `{Artist}`, `{Title}`, time — a Text mode, not a type |
| `Particles` | `Particles` | **Deferred** | Phase 2 |
| `DummyElement` | `.sbx` custom shader | **Planned** | Phase 2. Becomes our plugin story — see [`13`](13-EXTENSIBILITY.md) |
| `AppLogo` | — | **Dropped** | It draws *Avee's own* logo. Nothing to port |
| `3DBox` | 3D layers | **Deferred** | Phase 3. Folded into real 3D rather than a one-off box |
| `FOV` | Camera | **Deferred** | Phase 3, part of the camera model |
| `BlurEffect` | `Blur` | **Deferred** | Phase 2 |
| `MotionBlurEffect` | `MotionBlur` | **Deferred** | Phase 2 |
| `ZoomBlurEffect` | `ZoomBlur` | **Deferred** | Phase 2 |
| `RgbSplitEffect` | `RgbSplit` | **Deferred** | Phase 2 |
| `ColorCorrection` | `ColorCorrect` | **Deferred** | Phase 2 |
| `CurveEffect` | `Curves` | **Deferred** | Phase 2 |
| `MirrorEffect` | `Mirror` | **Deferred** | Phase 2 |
| `EdgeEffect` | `Edges` | **Deferred** | Phase 2 |
| `PixelEffect` | `Pixelate` | **Deferred** | Phase 2 |
| `TwirlEffect` | `Twirl` | **Deferred** | Phase 2, displacement family |
| `LiquifyEffect` | `Liquify` | **Deferred** | Phase 2, displacement family |
| `BulgePinchEffect` | `BulgePinch` | **Deferred** | Phase 2, displacement family |
| `Spherify` | `Spherify` | **Deferred** | Phase 2, displacement family |
| `Kaleidoscope` | `Kaleidoscope` | **Deferred** | Phase 2 |
| `GodraysEffect` | `Godrays` | **Deferred** | Phase 2. "Ray Effects" in the brainstorming |

### Avee — behaviour and systems

| Capability | Status | Notes |
|---|---|---|
| `MeasuredVar` — properties wired to audio | **Done** | Generalized into `Driver`. Stage 1 |
| Effects are nodes in the tree, not a separate stack | **Planned** | Adopted over AE's model. Stage 1 |
| Resolution-independent measurement (`Meter`) | **Planned** | Normalized to the smaller dimension. Stage 2 |
| Six blend modes (Alpha, PreMulAlpha, Add, AddAlpha, Screen, Over) | **Planned** | Stage 2 |
| Mask by explicit reference to another composition | **Deferred** | Phase 2. Better than AE's order-based mattes — see [`07`](07-UI-UX.md) |
| Hann window, bin smoothing, attack/decay, high-pass | **Planned** | Stage 3. Kept wholesale; only *when* it runs changes |
| Peak band index tracking | **Planned** | Stage 3 (`PeakBarIndex` measure) |
| Intro/outro fades per composition | **Deferred** | Phase 2 |
| Gradual GL resource creation across frames | **Deferred** | Phase 2, matters most on Android |
| Song library and player UI | **Deferred** | Phase 3. Open question in the brainstorming |
| Instant 60fps preview with no caching | **Planned** | Phase 1 exit criterion. The thing AE cannot do |
| `.viz` project import | **Planned** | Phase 2, ahead of `.aep` — see [`14`](14-VIZ-COMPATIBILITY.md) |
| Realtime-only, frame-rate-dependent smoothing | **Dropped** | Breaks preview/export parity. Replaced by deterministic offline analysis — see [`04`](04-AUDIO-ENGINE.md) |
| Blending in sRGB | **Dropped** | Causes clipped glows and banded gradients. We work in linear |
| Sprite-font text | **Dropped** | No kerning, ligatures or non-Latin scripts. FreeType + HarfBuzz instead |
| Writing unknown node types as `unk` | **Dropped** | Avee loses data it does not recognise. We round-trip it — see [`06`](06-PROJECT-FORMAT.md) |

### Avee — the measure vocabulary

All 20 measures from `MeasureDefs`. Our `Driver` decomposes each into a signal
plus a shaping chain, so these are **covered by composition rather than by
reimplementation** — which is also why we get combinations Avee never offered.

| Avee measure | Our equivalent |
|---|---|
| `Beat`, `Amount`, `Threshold`, `Smoothness`, `Speed` | Signal + shaping chain parameters |
| `BeatCamShakeMore` / `BeatCamShakeLess` | `beat` → shake, differing only in amount |
| `BeatCamShakeRotMore` / `BeatCamShakeRotLess` | Same, on rotation |
| `ConstantShake`, `ConstantShakeMore` | `constant` signal → shake |
| `ConstantShakeRotLess` / `ConstantShakeRotMore` | Same, on rotation |
| `BeatRandomShake` | Seeded random stage — **Done** (Stage 1) |
| `BeatTriggerAnim` | Beat → keyframe trigger. **Planned**, needs the node tree |
| `TrackPosition`, `TotalTime`, `TotalTimeBackward` | Time signals. **Planned** Stage 3 |
| `TotalTimeAndBeat`, `TotalTimeWhenPlaying` | Time signals, gated on playback |
| `IsPlaying` | Transport signal |
| `PeakBarIndex` | Analysis signal, Stage 3 |
| `Constant`, `Nothing`, `X`, `Y` | `constant` signal — **Done** |

---

## After Effects

AE ships **369 `.aex` plugins** in this install. Matching all of them is not a
goal and never was; most are print, broadcast-legacy, or film-finishing tools
with no bearing on music visuals. What follows is everything named in
[`BRAINSTORMING.md`](BRAINSTORMING.md) plus the structural features that make AE
what it is.

### Structure and workflow

| AE capability | Status | Notes |
|---|---|---|
| Keyframes with bezier handles | **Done** | Stage 1 |
| Hold / linear / eased interpolation | **Done** | Stage 1 |
| Separate in and out interpolation per keyframe | **Done** | Stage 1 |
| Timeline panel with playhead and zoom | **Planned** | Stage 5 |
| Layers | **Planned** | Our node tree. Stage 1 |
| Nested compositions / precomps | **Deferred** | Phase 2 |
| Project file explorer panel | **Planned** | Stage 5 |
| Live preview | **Planned** | Phase 1 exit criterion, and far faster than AE's |
| Render / export | **Planned** | Stage 6 |
| One-click render button, no render queue | **Planned** | Stage 6. A deliberate improvement on AE |
| Multiframe rendering | **Deferred** | Phase 2 |
| Undo / redo | **Planned** | Stage 1 command stack |
| 2D transforms (position, scale, rotation, anchor, opacity) | **Planned** | Stage 1 |
| 3D layers and cameras | **Deferred** | Phase 3. AE's model preferred over Avee's `3DBox` |
| Motion paths — spatial bezier separate from temporal | **Deferred** | Phase 2, but the data model must leave room now |
| Masks and track mattes | **Deferred** | Phase 2, using Avee's explicit-reference model |
| Expression language | **Deferred** | Phase 4 — see [`13`](13-EXTENSIBILITY.md). Driver system covers most uses first |
| Plugin / effect imports | **Planned** | `.sbx`, Phase 2. OpenFX Phase 4 |
| `.aep` project import | **Deferred** | Phase 3 — see [`12`](12-AEP-COMPATIBILITY.md) |
| Hosting AE's own `.aex` binaries | **Dropped** | Windows-only, structurally impossible on Android, so a scene using one stops being portable. Full reasoning in [`16`](16-PLUGIN-HOSTING.md) |
| Collaboration / multi-user editing | **Deferred** | Phase 4. New, in neither parent |
| Variable preview playback speed | **Deferred** | Phase 2. From the brainstorming; neither parent does it well |

### Effects named in the brainstorming

| AE effect | Status | Notes |
|---|---|---|
| Audio Spectrum | **Planned** | Stage 4, merged with Avee's `Bars` and its smoothing options |
| Sound Keys | **Planned** | Phase 2. Our `Driver` already is this, generalized |
| Text | **Planned** | Phase 1 basic, Phase 2 typographic |
| Particles | **Deferred** | Phase 2 |
| Ray effects (godrays) | **Deferred** | Phase 2 |
| Blur | **Deferred** | Phase 2 |
| Zoom blur | **Deferred** | Phase 2 |
| Colorama / colour correction | **Deferred** | Phase 2 |
| Liquify / turbulent displace | **Deferred** | Phase 2 |
| FOV | **Deferred** | Phase 3, camera |
| Pixelation | **Deferred** | Phase 2 |
| Edge effect | **Deferred** | Phase 2 |
| Custom GLSL element | **Planned** | `.sbx`, Phase 2. Avee's `DummyElement` done properly |

### Deliberately out of scope

Recorded here so the question is answered once. From
[`00-VISION.md`](00-VISION.md)'s non-goals:

| Not doing | Why |
|---|---|
| Video editing (trimming, transitions, NLE work) | Different tool. Use a video editor |
| General 3D modelling | Different tool. We do 3D *layers*, not meshes |
| A DAW or audio editor | We read audio, we do not author it |
| Hosting AE plugins | See [`16`](16-PLUGIN-HOSTING.md) |
| macOS and iOS | Decided at the start of the project |
| Cloud accounts, subscriptions, telemetry | Against the point of the project |
| Roto Brush, Puppet, Content-Aware Fill, 3D Camera Tracker | Film-finishing tools. Enormous, and irrelevant to music visuals |

---

## Keeping this honest

Check this ledger at the end of every phase. Two failure modes to watch for:

1. **Quiet drift.** A feature slips from Planned to Deferred to forgotten
   without anyone deciding. The status column exists to make that visible.
2. **A false sense of coverage.** The Avee element table above is only correct
   because it was read from smali. The incomplete `java_src/` decompile would
   have had us declare victory eleven effects early. When verifying parity, go
   to the authoritative source, not the convenient one.
