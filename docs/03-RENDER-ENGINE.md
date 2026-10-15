# 03 — Render Engine

## Goals, in priority order

1. **Preview and export produce identical pixels.** Non-negotiable. If these can
   diverge, users cannot trust the preview and the tool is worthless.
2. **60fps preview at 1080p** on your RTX 2070 with a realistic scene (say 20
   nodes, two blurs, 256 bars).
3. **Export faster than realtime** at 1080p60. A 3-minute track should export in
   well under 3 minutes.
4. **Portable to GLES 3.2** without a second renderer.

## The RHI

`gpu/` wraps OpenGL in about thirty functions. Handles are opaque integers, not
pointers, which makes them trivially copyable, safe to put in command lists, and
usable as keys in the resource cache.

```cpp
struct TextureHandle     { uint32_t id; };
struct FramebufferHandle { uint32_t id; };
struct ShaderHandle      { uint32_t id; };
struct BufferHandle      { uint32_t id; };
```

The abstraction is intentionally thin. We are not hiding OpenGL's model, we are
giving it a typed surface so renderers cannot make global state changes behind
each other's backs. The things it owns and nothing else may touch: the bound
framebuffer, blend state, viewport, and bound program.

Shaders are written once in GLSL with a preprocessor shim:

```glsl
// render/shaders/bars.frag
#include "common.glsl"
uniform sampler2D uSpectrum;
uniform vec4      uColorA;
// ...
```

The shim prepends `#version 430 core` on desktop or `#version 320 es` plus
`precision highp float;` on Android, and resolves `#include`. That is the entire
cross-platform shader story, and it works because we deliberately restricted
ourselves to the GL 4.3 ∩ GLES 3.2 feature set.

## Frame graph

Each frame, the evaluator produces a list of passes with explicit input and
output dependencies. The graph then resolves execution order and, importantly,
**aliases render targets** so a scene with six effects does not allocate six
full-resolution framebuffers.

```
[Bars L] ──┐
           ├──> [Group: Visualizer] ──> [Blur: Glow] ──┐
[Bars R] ──┘                                           │
                                                       ├──> [Composite] ──> [Grade] ──> screen
[Background] ──────────────────────────────────────────┤
[Title] ───────────────────────────────────────────────┘
```

Avee does the equivalent by giving each non-final `Composition` its own
`VFrameBuffer` allocated in `onCreateGLResources` and holding it forever. That is
simple and it works, but it means framebuffer memory scales linearly with
composition count, and at 4K each one is 32 MB. A pooled frame graph solves this:
targets are requested per-pass, recycled when their last reader finishes, and
total memory tracks graph *depth* rather than node count.

### Render target pool

Keyed by `(width, height, format, samples)`, with a free list. Allocation happens
on first use and persists across frames, so steady-state rendering performs no
GL allocation at all. Targets unused for roughly 300 frames get released, which
handles the case where a user deletes a heavy effect and expects the VRAM back.

## Resolution independence

The preview renders at whatever size the viewport happens to be; the export
renders at project resolution. These must agree, which means **no property may
be expressed in pixels.**

This is the single most common source of "it looked right in preview" bugs, and
Avee solved it well with its `Meter` class. Everything normalizes against the
*smaller* screen dimension:

```java
// Avee's Meter.measureScreenSpace — the reference behavior
float measureScreenSpace(float f) {
    int d = min(getScreenHeight(), getScreenWidth());
    return f * d;
}
```

We adopt the same convention. A property value of `0.5` means "half the smaller
dimension" at any resolution, so a scene authored in a 400px preview panel
exports correctly at 4K. Stroke widths, blur radii, particle sizes, and font
sizes all follow this rule. The only pixel-valued things in the entire system
are the framebuffer dimensions themselves.

Blur radius deserves a specific note, because a Gaussian blur with a fixed
*kernel* and a scaled *radius* changes character with resolution. The radius must
scale with resolution **and** the sample count must scale with it, or a 4K export
will show banding that the preview never had.

## Blend modes

Avee ships six, which turns out to be most of what a visualizer actually needs:

| Mode | Use |
|---|---|
| `Alpha` | Standard `src.a` over `dst` |
| `PreMulAlpha` | Premultiplied, correct for filtered textures |
| `Add` | Glows, light, energy |
| `AddAlpha` | Additive weighted by alpha |
| `Screen` | Softer additive, no clipping blowout |
| `Over` | Added by the Avee RE project; alpha-preserving composite |

We start with these six and add the Photoshop set (Multiply, Overlay, Soft Light,
Difference) in Phase 2, which needs shader-based blending rather than fixed-function
`glBlendFunc` since GL cannot express them natively.

**Work in linear color space, not sRGB.** Avee does not, and it is visible — its
additive glows blow out harshly and its gradients band. Render to an
`RGBA16F` target, blend in linear, convert to sRGB only at final output. This
costs bandwidth, and on a 2070 that is an easy trade for correct-looking light.

## Preview vs export

The only differences:

| | Preview | Export |
|---|---|---|
| Clock | Wall clock, tied to audio playback | Virtual, `frame / fps` exactly |
| Resolution | Viewport size | Project resolution |
| Audio analysis | Cache lookup at current time | Cache lookup at frame time |
| Output | Swap chain | `glReadPixels` → FFmpeg |
| Dropped frames | Allowed, skip ahead | Impossible by construction |

Everything else — evaluator, frame graph, renderers, shaders — is shared code.

### Why export cannot drift

The virtual clock is the key. Preview at 60fps that stutters to 45 simply
evaluates at different `t` values and looks fine. Export evaluates at exactly
`t = frame / fps` for every frame with no gaps. Because audio analysis is
precomputed and *indexed by time* rather than accumulated frame to frame, both
paths read the same values at the same `t`.

This is precisely where a naive port of Avee would break. Avee's smoothing is
stateful and frame-rate dependent:

```java
// Avee's temporal smoothing — accumulates across frames
fftMagSmooth[i] = fftMagSmooth[i] * smoothFactor
                + fftMag[i] * (1 - smoothFactor);
```

Run that at 45fps and you get different values than at 60fps, so a dropped
preview frame would permanently desynchronize preview from export. **Our
smoothing is computed during the offline analysis pass at a fixed analysis rate
and stored**, so it is a pure function of time. Any node that needs frame-to-frame
state must declare it, and the evaluator must be able to reconstruct that state
deterministically from a seek. See [`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md).

### Export pipeline

```
for frame in 0..frameCount:
    t = frame / fps
    evaluate(scene, t, analysisCache)
    renderFrameGraph() → RGBA16F target
    tonemap + convert → RGB8/NV12
    readback via PBO (double-buffered, non-blocking)
    push to FFmpeg encoder (NVENC h264/h265, or x264 for compatibility)
```

Use **pixel buffer objects with double buffering** for readback. A naive
`glReadPixels` stalls the pipeline waiting for the GPU to finish, and on a long
export that stall dominates total time. With two PBOs you read frame N-1 while
the GPU renders frame N, and throughput roughly doubles.

Audio is muxed in from the decoded source without re-encoding where the container
allows it.

## Performance budget at 1080p60

16.6 ms per frame total:

| Stage | Budget | Notes |
|---|---|---|
| Evaluate | 0.5 ms | Pure CPU math; if it exceeds this, the property system has an indirection problem |
| Frame graph build | 0.1 ms | Should be nearly free; reuse last frame's plan when the graph is unchanged |
| Geometry upload | 1.0 ms | Persistent mapped buffers, no per-frame allocation |
| Draws | 10 ms | The actual work |
| Blur passes | 3 ms | Separable, at half resolution where acceptable |
| Present | 1 ms | |

The two things most likely to blow this budget, based on how Avee is built:
**blur** (fix with separable kernels and downsampled ping-pong, which Avee's
`BlurStackEffectElement` already does) and **per-bar draw calls** (fix with a
single instanced draw for all bars, rather than Avee's per-segment approach).

## Testing a renderer

Hard, but tractable:

- **Golden image tests.** Render fixed scenes headless, compare against committed
  PNGs with a perceptual tolerance. Catches regressions that unit tests cannot.
- **Determinism test.** Render the same frame twice, require bit-identical
  output.
- **Preview/export parity test.** The most valuable test in the project: render
  frame N through the preview path and the export path, require them to match
  within tolerance. This directly guards goal #1.

Headless GL on CI via EGL with a software driver (`llvmpipe`) is slow but
sufficient for correctness.
