# 08 — Roadmap

Phases are ordered by **risk reduction**, not by how exciting the features are.
The things most likely to kill the project come first.

## Phase 0 — Planning *(current)*

- [x] Study the Avee Player source, extract architectural lessons
- [x] Write the documentation set
- [ ] Confirm the animation model (Option C)
- [ ] Choose a name
- [ ] Choose a license
- [ ] Create the public repository

**Exit:** the docs are agreed and the repo is public.

## Phase 1 — Vertical slice

The goal is a thin but complete path through every layer, proving the concept
end to end. Breadth is explicitly sacrificed.

**Target:** drop in an MP3, get reactive bars plus a keyframed logo, export a
1080p60 MP4 that matches the preview frame for frame.

| Area | Scope |
|---|---|
| Build | CMake + Ninja, Linux and Windows, CI green |
| `core/` | Node tree, property registry, keyframes, drivers, evaluator, JSON save/load, undo |
| `gpu/` | GL 4.3 backend, textures, FBOs, shaders, blend modes, frame graph with target pooling |
| `audio/` | FFmpeg decode, miniaudio playback, offline FFT analysis, mel bands, spectral flux onsets |
| `render/` | Three node types only: `SpectrumBars`, `Image`, `SolidColor` |
| `export/` | Deterministic offline render, FFmpeg/NVENC encode, audio mux |
| `app-desktop/` | ImGui shell, viewport, scene tree, inspector, basic timeline |

**Deliberately excluded from Phase 1:** text rendering, particles, blur, nested
compositions, beat grid and tempo, motion paths, Android.

**Exit criteria, concretely:**

1. The preview/export parity test passes.
2. 60fps preview at 1080p with 128 bars, a background, and a logo.
3. A 3-minute track exports in under 3 minutes.
4. A project round-trips through save and load with no loss.

Phase 1 is the whole bet. If parity and performance work here, the rest is
content. If they do not, we find out after a few months rather than a few years.

## Phase 2 — A real tool

Now broaden, now that the foundation is proven.

| Area | Scope |
|---|---|
| Text | FreeType + HarfBuzz, proper shaping, metadata fields (`{Artist}`, `{Title}`) |
| Effects | Blur (separable, downsampled), RGB split, color grading, mirror, motion blur |
| Compositions | Nesting, precomps, composition-as-source |
| Timeline | Tempo estimation, beat grid, snapping, markers |
| Animation | Motion paths, curve editor, keyframe easing UI |
| Drivers | Full shaping chain, driver presets named after Avee measures |
| Particles | Emitters, basic forces, audio-reactive spawning |
| Platform | Windows packaging, AppImage, auto-update check |

**Exit:** someone who is not us makes a video they are willing to post publicly.
That is the real test and no internal metric substitutes for it.

## Phase 3 — Android and community

| Area | Scope |
|---|---|
| Android | NDK build of the core, Compose UI, AAudio, GLES 3.2 backend, APK |
| Sharing | `.specpack` import/export, preset browser |
| Analysis | Downbeat and section detection |
| Effects | Photoshop-family blend modes, shader-based |
| Performance | GPU profiling pass, compute shader paths where they help |

**Exit:** Avee users are migrating of their own accord.

## Phase 4 — Depth

Speculative, ordered by likely value:

- Extensibility, in three tiers — property expressions, scriptable effect nodes
  (shader + manifest), and editor extensions. Sandboxed and deterministic on the
  render path. See [`13-EXTENSIBILITY.md`](13-EXTENSIBILITY.md), which absorbs the
  old "expression language", "plugin API for custom nodes and shaders", and
  "shader node with live GLSL editing" items
- 2.5D camera with Z-positioned layers
- Masks and track mattes
- MIDI and stem-based input, so reactions come from individual instruments
  rather than a mixed spectrum. Potentially the most interesting feature in the
  entire project, since it is the one no competitor can easily copy
- After Effects project import — scoped `.aepx` subset plus an optional AE-side
  exporter script, gated behind a research spike. See
  [`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)

## Time

Honest estimates, assuming part-time work by a small group. Multiply by two if
anyone is learning C++ or graphics programming as they go.

| Phase | Estimate |
|---|---|
| 0 | Days |
| 1 | 3–6 months |
| 2 | 6–12 months |
| 3 | 3–6 months |
| 4 | Ongoing |

The largest single risks, in order:

1. **Scope creep in Phase 1.** Everything in Phase 2 will feel urgent. It is not.
2. **The renderer eating all the time.** Graphics work expands to fill available
   hours. The performance budget in
   [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md) exists to decide when to stop.
3. **Contributors arriving before the architecture is stable.** A public repo at
   the end of Phase 0 is good for the project but means early contributors will
   touch code that is still moving. Label issues carefully and keep module
   boundaries clean so parallel work is possible.
