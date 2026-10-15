# 01 — Architecture

## The one rule

**The core knows nothing about the GPU, the OS, the UI, or the file system.**

Everything that makes a scene what it is — the element tree, properties,
keyframes, audio bindings, evaluation, undo — is plain C++20 with no
dependencies beyond the standard library and a math library. The renderer, the
editor, and the Android shell are all consumers of it.

This is the single most important structural decision, because it is what makes
three platforms and two very different UIs affordable. It also makes the core
unit-testable without a window or a GL context, which matters enormously when
contributors are running CI.

Avee did not do this. Its elements call `GLES20.glClearColor` directly and take
`android.graphics.PointF` in their public API, which is exactly why Avee is
Android-only and why its render code cannot be tested. We are not repeating that.

## Layers

```
┌─────────────────────────────────────────────────────────────┐
│  app-desktop/            │  app-android/                    │
│  Dear ImGui editor       │  Kotlin + Compose UI             │
│  GLFW window, Win/Linux  │  JNI bridge into core            │
└─────────────────────────────────────────────────────────────┘
                              ↓ depends on
┌─────────────────────────────────────────────────────────────┐
│  render/        the element renderers: bars, text, image,    │
│                 particles, blur, RGB split, color grade      │
├─────────────────────────────────────────────────────────────┤
│  export/        deterministic offline render → FFmpeg/NVENC  │
└─────────────────────────────────────────────────────────────┘
                              ↓ depends on
┌─────────────────────────────────────────────────────────────┐
│  gpu/           thin RHI: textures, framebuffers, shaders,   │
│                 vertex buffers, blend state, frame graph     │
│                 backends: GL 4.3 core · GLES 3.2             │
├─────────────────────────────────────────────────────────────┤
│  audio/         decode (FFmpeg) · playback (miniaudio)       │
│                 analysis: FFT, bands, beat, loudness         │
├─────────────────────────────────────────────────────────────┤
│  platform/      files, threads, clock, logging, dialogs      │
└─────────────────────────────────────────────────────────────┘
                              ↓ depends on
┌─────────────────────────────────────────────────────────────┐
│  core/          scene graph · property system · keyframes    │
│                 audio drivers · evaluator · serialization    │
│                 undo · command stack                         │
│                 *** no GL, no OS, no UI, no I/O ***          │
└─────────────────────────────────────────────────────────────┘
```

Dependencies point downward only. `core/` compiling against anything above it is
a bug, and we should enforce that in CI with a simple include-scanning script
rather than trusting discipline.

## Module responsibilities

### `core/`

The scene is a tree of **nodes**. Following Avee's best idea, effects are nodes
in the tree rather than a separate "effect stack" concept — a blur is a node
that happens to consume its siblings' output. This keeps one uniform traversal
and one uniform property UI instead of two parallel systems.

```
Project
└── Composition "Main"  (1920×1080 @ 60fps, duration 3:42)
    ├── Layer  "Background"     → ImageNode
    ├── Group  "Visualizer"
    │   ├── Layer "Bars L"      → SpectrumBarsNode
    │   ├── Layer "Bars R"      → SpectrumBarsNode
    │   └── Effect "Glow"       → BlurNode(mode=additive)
    ├── Layer  "Title"          → TextNode
    └── Effect "Grade"          → ColorGradeNode
```

Every node exposes its parameters through a **property registry**, which is how
one generic inspector panel can edit any node type without the UI knowing what
a `SpectrumBarsNode` is. Avee does this with
`onReadCustomization`/`onApplyCustomization`, and it works well enough that the
entire Avee settings UI is generic. We take the idea and make it declarative:

```cpp
void SpectrumBarsNode::describe(PropertyRegistry& r) {
    r.group("Shape");
    r.range ("barCount",   &barCount,   1, 512);
    r.range ("barWidth",   &barWidth,   0.0f, 1.0f);
    r.range ("barSpacing", &barSpacing, 0.0f, 1.0f);
    r.group("Audio");
    r.range ("smoothing",  &smoothing,  0.0f, 0.99f);
    r.select("scaleMode",  &scaleMode,  {"Linear", "Log", "Mel"});
    r.group("Look");
    r.color ("colorA",     &colorA);
    r.enumv ("blendMode",  &blendMode);
}
```

One function call per parameter gives us, for free: the inspector UI, keyframe
support, audio-driver support, serialization, undo, and a stable name for the
project file. That last point matters more than it looks — a declarative registry
means the save format is derived from the same source of truth as the UI, so
they can never drift apart.

### `gpu/`

A deliberately small abstraction. We are not writing a general-purpose engine;
we need textures, framebuffers, shaders, vertex buffers, blend state, and a way
to order passes. Roughly thirty functions.

It exists only so that `render/` never writes a raw GL call, which is what lets
us add a Vulkan or D3D12 backend later without touching renderer code. Details
and the frame graph design are in [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md).

### `audio/`

Two very different consumers, which is the subtlety here:

- **Playback** needs low latency and a real-time thread that must never block.
- **Analysis** needs to be *reproducible*, because export must match preview.

The resolution is that analysis never runs on the audio thread. See
[`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md).

### `render/`

One renderer per node type. Each takes evaluated parameters plus a GPU context
and emits draw calls. Renderers are stateless with respect to the scene — they
receive a flat struct of already-evaluated numbers and never reach back into the
node tree. This is what allows the render pass to run on a worker thread while
the UI thread is editing the scene.

### `export/`

Drives the same evaluator and the same renderers as preview, but from a virtual
clock, reading back framebuffers and feeding FFmpeg. The fact that it shares
code with preview is not an optimization, it is the correctness guarantee.

## Frame flow

Understanding this sequence is most of understanding the codebase.

```
 1. Clock advances           → t = 71.5s, frame 4290
 2. Audio analysis lookup    → bands[256], rms, beat envelope at t
                               (read from the analysis cache, never computed here)
 3. Evaluate                 → walk the tree, resolve every property:
                                 keyframe curve at t, blended with audio drivers
                               output: flat EvaluatedFrame (struct-of-arrays)
 4. Build frame graph        → passes, render targets, dependency order
 5. Execute                  → gpu/ issues draw calls per pass
 6. Present                  → swap buffers (preview) or read back (export)
```

Steps 3 and 5 are the hot path and the only two places performance work belongs.
Step 3 is pure CPU math over a compact array and should be measured in
microseconds; if it is not, the property system has an indirection problem.

The critical property of this design: **steps 1–5 are identical in preview and
export.** The only difference is where the clock comes from and where the pixels
go. Any divergence between what you preview and what you export is therefore a
bug with exactly one possible cause, rather than an entire category of mystery.

## Threading

| Thread | Work | Rules |
|---|---|---|
| UI | ImGui, input, scene edits | Owns the scene graph, sole writer |
| Render | Evaluate + draw | Reads an immutable scene snapshot |
| Audio | Playback callback | Lock-free, no allocation, no logging |
| Analysis pool | FFT over the track | Writes to the cache, worker threads |
| Export | Offline render + encode | Own GL context, own clock |

The UI thread hands the render thread an immutable snapshot each frame rather
than sharing the live tree. Copying a scene is cheap (a few hundred KB at worst)
and it eliminates the entire class of editing-during-render data races. If
snapshot cost ever shows up in a profile, persistent data structures with
structural sharing are the escape hatch, but we should not start there.

## Layout on disk

```
SpectraBlack/
├── core/        include/  src/  tests/
├── gpu/         include/  src/  backends/{gl, gles}/
├── audio/       include/  src/  tests/
├── render/      include/  src/  shaders/
├── export/      include/  src/
├── platform/    include/  src/{linux, windows, android}/
├── app-desktop/ src/  ui/
├── app-android/ (Gradle project, JNI bridge)
├── third_party/ (CMake FetchContent, nothing vendored by hand)
├── assets/      fonts, icons, default scenes
├── docs/
└── CMakeLists.txt
```

Every module owns its tests. A contributor touching the FFT should be able to
run `ctest -R audio` and get an answer in seconds without building the editor.
