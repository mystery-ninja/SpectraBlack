# 15 — Build Order

What to actually do, in order, and who can work on what at the same time.

[`08-ROADMAP.md`](08-ROADMAP.md) says *what ships* in each phase. This document
says *what to do Monday morning*. If you are picking up a task, start here.

## How to read this

Stages are ordered by **dependency**, not by importance or by how visible the
result is. A stage is ready when the things it needs exist, and not before.

Within a stage, tasks are small enough that one person can finish one in a
sitting or two. Check them off in this file as they land — it is the shared
progress board.

> **Rule:** do not start a stage whose dependencies are unfinished. The ordering
> exists because building on an unstable foundation means building twice.

## Why the GUI is not first

The intuitive order is "make the interface, then make it work." It is wrong here,
for one specific reason: the inspector panel is **generated** from the property
registry ([`01-ARCHITECTURE.md`](01-ARCHITECTURE.md)). One `describe()` call per
parameter produces the UI, the keyframe support, the driver support, and the
serialization.

Build the GUI first and you hand-write panels for a data model that does not
exist, then throw them away when it does. Build the registry first and most of
the GUI writes itself.

The same logic applies to the timeline — it edits keyframes, so keyframes have to
exist first.

**But visible progress matters for morale**, so the order below front-loads one:
Stage 4 is bars moving to music, and it arrives before any editor work. That is
the "it's alive" moment and it is deliberately early.

---

## Stage 0 — Foundations

**Unblocks everyone. Nothing else starts until this is done.** One person, a few
days.

- [ ] `CMakeLists.txt` with module directories: `core`, `gpu`, `audio`,
      `render`, `export`, `platform`, `app-desktop`
- [ ] CMake presets for `linux-debug`, `linux-release`, `windows-debug`
- [ ] `FetchContent` wiring for GLFW, GLM, doctest (more added as needed)
- [ ] A hello-world binary that builds and runs
- [ ] `doctest` wired in, one trivial test passing via `ctest`
- [ ] `.clang-format` committed
- [ ] GitHub Actions: Linux build + test, clang-format check
- [ ] CI job that **fails if anything in `core/` includes GL, OS, or UI headers**

Install first: `sudo apt install cmake ninja-build clang-format clang-tidy`
plus the X11 and FFmpeg dev packages from [`02-TECH-STACK.md`](02-TECH-STACK.md).

**Exit:** a new contributor can clone, build, and run the tests in under five
minutes with no manual setup.

That last CI job is the one that protects the architecture long-term. Everything
else in Stage 0 is conventional.

---

## Stage 1 — Core data model

**Pure C++ logic, no GPU, no UI, fully unit-testable.** This is the heart of the
app and the most important code in the project.

- [ ] `Property<T>` — static value, keyframe track, driver list
- [ ] `Keyframe<T>` and `KeyframeTrack<T>` with `Hold`, `Linear`, `Bezier`
- [ ] Keyframe evaluation at arbitrary `t`, with bezier handles
- [ ] `Node` base + `Group`/`Composition` containers; the tree
- [ ] `PropertyRegistry` and `describe()` — the declarative property system
- [ ] `Driver` struct: signal reference, shaping chain, amount, blend op
- [ ] Blend operators: `Add`, `Multiply`, `Replace`, `Max`, `Min`
- [ ] Shaping chain: remap, curve, attack/decay, delay, seeded random
- [ ] Evaluator: flatten the tree into `EvalPlan`, evaluate to a flat output array
- [ ] JSON serialization, round-trip lossless
- [ ] Unknown-key and unknown-node-type preservation
      ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md))
- [ ] `Command` stack: apply, revert, `mergeWith` coalescing
- [ ] Tests for all of the above

Audio signals are **stubbed** here — a driver asks for `bass` and gets a fake
sine wave. That is enough to prove the whole evaluation path, and it keeps Stage 1
independent of Stage 3.

**Exit:** you can build a scene in code, evaluate every property at any `t`, save
it, reload it, and undo an edit — all headless, all tested, no window.

---

## Stage 2 — First pixels

**Independent of Stage 1.** Can run in parallel.

- [ ] GLFW window with a GL 4.3 core context
- [ ] glad loader
- [ ] RHI handles: `TextureHandle`, `ShaderHandle`, `BufferHandle`,
      `FramebufferHandle`
- [ ] Shader compile and link, with the `#version` cross-platform shim
- [ ] `#include` resolution in shader sources
- [ ] Vertex buffers, persistent mapped
- [ ] Textures, with `stb_image` loading
- [ ] Framebuffers, `RGBA16F`, linear color
- [ ] The six blend modes from [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md)
- [ ] Fullscreen quad, and a textured quad with transform
- [ ] Frame graph: pass list, dependency ordering, render target pooling
- [ ] sRGB conversion at final output

**Exit:** a window showing textured quads compositing with correct blend modes in
linear color at a stable 60fps, with zero GL allocation per frame in steady state.

---

## Stage 3 — Audio

**Independent of Stages 1 and 2.** Can run in parallel.

- [ ] FFmpeg decode of MP3/FLAC/WAV/OGG to interleaved float PCM
- [ ] miniaudio playback, ring buffer fed by the decoder
- [ ] Playback clock, with device latency offset and a user offset
- [ ] PFFFT integration, 2048-point real FFT
- [ ] Hann window on input
- [ ] Offline analysis pass at a fixed 200 Hz hop, chunked across worker threads
- [ ] Mel band mapping, with linear and log as options
- [ ] Spectral smoothing across bins, Hamming-weighted
- [ ] Per-band asymmetric attack/decay
- [ ] **Spectral tilt compensation** (`Flat` / `Pink` / `Perceptual`) — see the
      bass discussion in [`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)
- [ ] Whole-track normalization statistics
- [ ] `AnalysisFrame` cache, queryable by time with interpolation
- [ ] Cache persistence to disk, keyed by audio file content hash
- [ ] Spectral flux onset detection with a centered adaptive threshold
- [ ] Tests: sine in the right band, click train at known BPM, silence yields
      zeros not NaNs, identical output across thread counts

**Exit:** load a track, play it, and query any signal at any `t` with
bit-identical results on repeat. Determinism is the whole point of this stage.

---

## Stage 4 — It's alive

**Needs Stages 1, 2, and 3.** The first time the project is recognisably itself.

- [ ] Replace the Stage 1 stub signals with the real analysis cache
- [ ] `SolidColor` node
- [ ] `Image` node
- [ ] `SpectrumBars` node, **one instanced draw call for all bars**
- [ ] Resolution-independent measurement, normalized to the smaller dimension
- [ ] A hardcoded demo scene: background, bars, logo
- [ ] Logo scale driven by `beatPulse` with `Multiply` blend

**Exit:** a window with bars dancing to a real track and a logo pulsing on the
beat, built from the real evaluator. No editor yet — the scene is defined in code.

Do not skip the hardcoded demo scene. It is the thing you show people, and it is
the proof that the three parallel stages actually fit together.

---

## Stage 5 — The editor

**Needs Stage 4.** Now the GUI has a data model to edit and something to show.

- [ ] Dear ImGui with the docking branch, wired to our GL backend
- [ ] Viewport panel rendering the frame graph output
- [ ] Transport: play, pause, seek, spacebar from any focus
- [ ] Scene tree panel: select, reorder, add, delete, rename
- [ ] **Inspector generated from `PropertyRegistry`** — no per-node UI code
- [ ] Sliders: coarse drag, fine with modifier, double-click to type, no clamping
      on typed values
- [ ] Right-click menu: reset, add keyframe, add driver
- [ ] Timeline panel: ruler, playhead, zoom, scroll
- [ ] Waveform display in the timeline
- [ ] Keyframe tracks: display, select, drag, delete
- [ ] Driver rows under their property, with inline amount and enable toggle
- [ ] Undo and redo wired to the command stack, including during playback
- [ ] Project save and load through the UI, with autosave
- [ ] Frame time and preview-resolution readout

**Exit:** you can build Stage 4's demo scene by hand, from an empty project,
without writing code.

---

## Stage 6 — Export

**Needs Stage 5.** Closes Phase 1.

- [ ] Virtual clock render loop, `t = frame / fps`
- [ ] Offscreen render at project resolution
- [ ] Double-buffered PBO readback
- [ ] Tonemap and convert to RGB8/NV12
- [ ] FFmpeg encode, NVENC with an x264 fallback
- [ ] Audio mux from the decoded source
- [ ] Export dialog: resolution, fps, codec, range, progress, cancel
- [ ] **Preview/export parity test** — render frame N both ways, require a match

**Exit — and the exit criteria for all of Phase 1:**

1. The parity test passes.
2. 60fps preview at 1080p with 128 bars, a background, and a logo.
3. A 3-minute track exports in under 3 minutes.
4. A project round-trips through save and load with no loss.

---

## Working in parallel

The useful part for a team of four. **Stages 1, 2, and 3 have no dependencies on
each other** and are deliberately designed that way.

```
Stage 0  ─────────────────────────────  everyone waits (a few days)
            │
            ├──► Stage 1  core          ─┐
            ├──► Stage 2  gpu           ─┼──► Stage 4 ──► Stage 5 ──► Stage 6
            └──► Stage 3  audio         ─┘
```

Suggested split, by what each stage actually demands:

| Stage | Suits someone who | Hardest part |
|---|---|---|
| 1 — core | Likes data structures and tests over pixels | Getting the property model right; everything depends on it |
| 2 — gpu | Wants to learn or already knows OpenGL | Frame graph and target pooling |
| 3 — audio | Finds DSP interesting | Determinism, and the tilt/normalization tuning |

Stage 1 is the highest-stakes and the best fit for whoever is most comfortable in
C++, because its mistakes are the most expensive to undo later.

### Rules for parallel work

1. **Agree the interfaces between stages before starting**, not while merging.
   Stage 4 is where they meet, and a disagreement found there is a rewrite.
2. **Each stage owns its own directory.** Minimal overlap means minimal conflicts.
3. **Push small and often.** See [`GIT-CHEATSHEET.md`](GIT-CHEATSHEET.md).
4. **Use a branch** once a stage is more than a sitting's work.
5. **Tests land with the code**, not afterward. Stages 1 and 3 are pure logic and
   have no excuse.

---

## After Phase 1

Roughly in value order, once the parity test is green. Detail in
[`08-ROADMAP.md`](08-ROADMAP.md).

| Next | Why this order |
|---|---|
| **Avee `.viz` import** | Cheapest big win, and it reaches the beachhead audience ([`14-VIZ-COMPATIBILITY.md`](14-VIZ-COMPATIBILITY.md)) |
| Nested compositions | Unblocks real scene complexity |
| Blur, RGB split, color grade | The effects people actually ask for |
| Text with FreeType + HarfBuzz | Large chunk of work on its own |
| Tempo estimation and beat grid | Unlocks keyframe snapping, the headline timeline feature |
| Particles | High visual payoff |
| Motion paths and the curve editor | Animation polish |
| Masking via explicit references | [`07-UI-UX.md`](07-UI-UX.md) |
| Windows packaging | Doubles the addressable platform |
| Android | A separate project in its own right ([`02-TECH-STACK.md`](02-TECH-STACK.md)) |
| AE `.aep` import | Hardest import, smallest audience — last ([`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)) |
| Expressions and extensions | [`13-EXTENSIBILITY.md`](13-EXTENSIBILITY.md) |

## Honest expectations

Stage 0 is days. Stages 1 through 3 are weeks each, in parallel. Stage 4 is fast
if the interfaces were agreed and slow if they were not. Stage 5 is the largest
single chunk of work in Phase 1, because editor UI always is. Stage 6 is small but
the parity test may surface problems that take a while to track down.

Phase 1 overall is 3–6 months part-time. Double it if anyone is learning C++ or
graphics programming as they go, and that is a normal and fine thing to be doing.

The two ways this goes wrong, in order of likelihood:

1. **Scope creep in Phase 1.** Everything in the "After Phase 1" table will feel
   urgent while you are grinding through Stage 5. It is not.
2. **Stage 5 eating everything.** Editor UI expands to fill available time. Ship
   it ugly, then improve it.
