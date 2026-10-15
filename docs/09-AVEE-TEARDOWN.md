# 09 — Avee Player Teardown

Findings from reading the decompiled Avee Player 1.2.101 source at
`/media/mystery/MysterYFile/Avee Player/avee-re-master`.

**Why this document exists:** Avee solved a lot of real problems well, and we
should take those solutions. It also made structural choices that are wrong for
our goals, and knowing exactly which is which saves us from both reinventing
good ideas and inheriting bad ones.

**Legal note:** we read this to understand *techniques*. We do not copy code. See
[`CONTRIBUTING.md`](../CONTRIBUTING.md).

## What it is

Apktool output of Avee Player 1.2.101, the last unobfuscated release. Java
sources under `java_src/`, plus smali, resources, and the manifest. The app is
`com.daaw.avee`, minSdk 16, targetSdk 30.

> **`java_src/` is an incomplete decompile. Use `smali/` as the authority.**
>
> The Java output contains 15 of the 26 registered element types. Every effect
> the RE fork added — Twirl, Liquify, Spherify, Kaleidoscope, Godrays, Edge,
> Pixel, ZoomBlur, BulgePinch, Curve, 3DBox, FOV — exists only under `smali/`.
> Searching `java_src/` for them returns nothing, which makes it easy to
> conclude they are not there at all.
>
> This cost us a wrong element count in [`00-VISION.md`](00-VISION.md) and
> nearly cost us eleven effects in the parity ledger. When checking what Avee
> can do, read `smali/com/daaw/avee/comp/Visualizer/Elements/`.

The RE project (by TheCubiq) has added features beyond stock Avee: a set of new
effects (color correction, twirl, liquify, kaleidoscope, FOV, zoom blur, pixels,
3D box, edges, godrays, spherify, curve, bulge/pinch), an "Over" blend mode, and
various UI improvements.

## Structure of the visualizer

```
com/daaw/avee/comp/
├── Visualizer/
│   ├── Elements/          node types
│   │   ├── Base/          Element, ElementGroup, MeasuredVar, MeasuredVar
│   │   ├── Segment/       the spectrum/waveform renderers and DSP
│   │   └── Particles/     particle system
│   ├── Graphic/           the GL layer
│   ├── Dsp/               filters, windows, curves
│   └── Path/              SVG and path interpolation
├── PcmProcess/            PCM buffering from ExoPlayer
├── VisualizerExporter/    MediaCodec video export
└── VisualizerCutomization/  the property UI  [sic]
```

Roughly 12,700 lines across the element system. Not large, which is encouraging —
the core of a good visualizer is not enormous.

### The complete element set

Twenty-six types, from `ElementsFactory` in smali. Tracked individually in
[`17-FEATURE-PARITY.md`](17-FEATURE-PARITY.md).

| Group | Types |
|---|---|
| Structure | `Root`, `Composition`, `AudioProvider` |
| Content | `Bars`, `Image`, `Text`, `StatText`, `Particles`, `DummyElement`, `AppLogo` |
| Blur | `BlurEffect`, `MotionBlurEffect`, `ZoomBlurEffect` |
| Colour | `ColorCorrection`, `CurveEffect`, `RgbSplitEffect` |
| Displacement | `TwirlEffect`, `LiquifyEffect`, `BulgePinchEffect`, `Spherify`, `Kaleidoscope`, `MirrorEffect` |
| Stylize | `EdgeEffect`, `PixelEffect`, `GodraysEffect` |
| Spatial | `3DBox`, `FOV` |

There is also `unk`, which is not a type but what `getTypeName` returns for an
element it does not recognise — see mistake 9 below.

## The good ideas (take these)

### 1. `MeasuredVar` — properties as rules

The best idea in the codebase, and the seed of our whole project. A property is
not a number, it is a measure name plus arguments:

```java
public class MeasuredVar {
    private String measure;      // "Beat", "TrackPosition", ...
    private Vec2f  measureArg;   // tuning
    private Vec2f  lastMeasured; // cached result
    public float measure(Meter meter) { ... }
}
```

Our `Driver` is the direct descendant of this, generalized so that the shaping is
parameterized rather than baked into the measure name. See
[`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md).

### 2. Effects are elements in the tree

`BlurEffect`, `RgbSplitEffect`, `MotionBlurEffect`, `MirrorEffect`, and
`ColorCorrection` are all registered in `ElementsFactory` alongside `Text`,
`Image`, and `Bars`:

```java
elementTypes.put("Bars",       SegmentElement.class);
elementTypes.put("BlurEffect", BlurStackEffectElement.class);
elementTypes.put("ColorCorrection", ColorCorrectionElement.class);
```

One tree, one traversal, one property UI. After Effects has a separate "effect
stack" concept per layer, which means two parallel systems for what is
fundamentally the same thing. Avee's approach is simpler and we adopt it.

### 3. The generic property system

`onReadCustomization` declares a node's properties; `onApplyCustomization` reads
them back:

```java
customPropertiesList.putPropertyFloatAsRange("introFadeDuration",
    getIntroFadeDurationSec(), "2_introOutro", 0.0f, 10.0f);
customPropertiesList.putSelectString("introBlendMode", ..., new String[]{...});
customPropertiesList.putPropertyIntAsCRGBA("introFadeColor", ..., "2_introOutro");
```

From this, Avee generates its entire settings UI and its save format. The
category string (`"2_introOutro"`) even carries sort order. This is why Avee can
have a consistent UI across every element type with no per-element UI code, and
it is the pattern behind our `PropertyRegistry`.

Our improvement is to make one declaration serve both directions, rather than
requiring the read and apply functions to be kept manually in sync — a
duplication that is clearly a bug source in Avee.

### 4. Resolution-independent measurement

The `Meter` class normalizes all spatial values against the smaller screen
dimension. We adopt this wholesale; see
[`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md).

### 5. The DSP chain

Well-chosen techniques throughout:

- Hann window on FFT input
- Hamming-weighted smoothing across neighboring bins (`fftSoftness`), which is
  what stops the bars looking like noise
- Asymmetric attack/decay temporal smoothing (`smoothFactor`, `smoothFactor2`)
- A high-pass filter on the spectrum (`SignalFilter1d.createHighPass`)
- Peak band index tracking with independent smoothing
- Windowed RMS

We keep all of it and only change *when* it runs.

### 6. The blend mode set

Six modes — `Alpha`, `PreMulAlpha`, `Add`, `AddAlpha`, `Screen`, `Over` — covering
most of what a visualizer needs. A useful minimum set to ship.

### 7. Gradual GL resource creation

`onCreateGradualGLResources(RenderState, int)` spreads expensive resource
creation across frames instead of hitching. A nice touch for a mobile app and
worth keeping for large texture loads.

## The mistakes (avoid these)

### 1. No abstraction over the GPU

Elements call `GLES20.glClearColor` and `GLES20.glTexParameteri` directly:

```java
// Composition.onRender
GLES20.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
GLES20.glClear(16384);
```

Note also the raw `16384` instead of `GL_COLOR_BUFFER_BIT` — an artifact of
decompilation, but the direct GL dependency is real. This is why Avee is
Android-only and why none of its render code can be tested. Our `gpu/` layer
exists specifically because of this.

### 2. No separation between engine and platform

`Element` takes `android.graphics.PointF` and `android.graphics.RectF` in its
public API. The scene model is welded to the Android framework. Our `core/` has
no platform dependencies at all.

### 3. Frame-rate-dependent stateful smoothing

Discussed at length in [`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md). Correct for a
live player, fatal for a tool that must export reproducibly.

### 4. Per-composition framebuffers held forever

Each non-final `Composition` allocates a `VFrameBuffer` in
`onCreateGLResources` and keeps it for its lifetime. Memory scales with
composition count rather than graph depth. Our pooled frame graph fixes this.

### 5. No linear color space

Blending happens in sRGB, which is why Avee's additive glows clip harshly and its
gradients band. We render to `RGBA16F` in linear and convert at output.

### 6. Sprite-font text only

`SpriteFont` with a texture atlas cannot do kerning, ligatures, or complex
scripts. Fine for `{Artist} - {Title}`, insufficient for a motion graphics tool.
FreeType plus HarfBuzz in Phase 2.

### 7. No timeline at all

The defining limitation, and the reason this project exists.

### 8. `MeasuredVar` conflates signal and effect

`BeatCamShakeMore` and `BeatCamShakeLess` are separate named measures that differ
only in magnitude. The shaping is baked into the identifier instead of being a
parameter, so users can only pick from what the authors anticipated. Our shaping
chain parameterizes it.

### 9. Unknown element types are written back as `unk`

`ElementsFactory.getTypeName` logs "Unknown instance type" and returns the
string `"unk"` for anything it does not recognise. An older Avee opening a
project made by a newer one therefore **destroys** the elements it cannot
identify, and saving makes the loss permanent.

Our project format preserves unknown node types and unknown keys verbatim
through a load/save round trip ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md)).
This matters more for us than it did for Avee, because we expect contributors
to add node types independently.

## Worth reading directly

If you are implementing a given subsystem, these files are the most instructive:

| File | Why |
|---|---|
| `Elements/Base/Element.java` | The node base class and property plumbing |
| `Elements/Base/MeasuredVar.java` | The reactive property concept |
| `Elements/Composition.java` | Nesting, framebuffers, intro/outro fades |
| `Elements/ElementsFactory.java` | The node type registry |
| `Segment/SegmentAudioSpectrumData.java` | The full DSP chain, ~795 lines |
| `Segment/SegmentAudioSpectrumDataPhaseS.java` | Phase spectrum variant, ~783 lines |
| `Graphic/BlendMode.java` | The blend mode set |
| `comp/Common/MeasureDefs.java` | The complete reactive vocabulary |
| `VisualizerExporter/` | MediaCodec export, for comparison with our FFmpeg path |

## Summary

Avee is a well-built app for what it is. Its element tree, generic property
system, resolution-independent measurement, and DSP chain are all worth taking.
Its platform coupling, realtime-only analysis, and absent timeline are exactly
the things we are built to fix.

The short version: **we are building Avee's ideas on an architecture that can
also do After Effects' job.**
