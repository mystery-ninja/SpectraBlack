# `gpu/`

A thin typed surface over OpenGL 4.3 core. Roughly thirty functions, so that
nothing above this module ever writes a raw GL call and a second backend stays
possible. The design and its reasoning are in
[`docs/03-RENDER-ENGINE.md`](../docs/03-RENDER-ENGINE.md).

## Layout

| Path | What is in it |
|---|---|
| `include/spectra/gpu/` | The public surface. No GL headers anywhere in it |
| `backends/gl/` | The only code in the project that calls OpenGL |
| `src/` | Backend-independent: the shader preprocessor, frame graph, target pool, quad renderer, image decode |
| `examples/quads.cpp` | `spectra_gpu_demo`, the Stage 2 exit criterion made runnable |
| `tests/` | `ctest -R gpu.` |

A GLES 3.2 backend for Android becomes `backends/gles/`, implementing the same
`device.hpp`. That is why the public headers carry no GL types.

## Building

```bash
cmake --preset linux-gpu      # or add -DSPECTRA_ENABLE_GPU=ON to any preset
cmake --build --preset linux-gpu
./build/linux-gpu/bin/spectra_gpu_demo [image.png]
```

The glad loader is generated at configure time, which needs Python with
Jinja2. The generated code has no runtime dependency on either.

## Pieces

**`shader_source.hpp`** — one GLSL source, two platforms. The preprocessor owns
the `#version` line, resolves `#include` once per translation unit, and keeps a
file table so a driver log reading `0:42` can be reported as `bars.frag:42`.
Touches no files and needs no context.

**`device.hpp`** — textures, buffers, shaders, framebuffers, blend state,
viewport, draws. Handles pack a slot index with a generation counter, so using
a destroyed resource resolves to nothing rather than to whatever took its
place. Dynamic buffers are persistently mapped where the driver allows it.

**`framegraph.hpp`** and **`target_pool.hpp`** — passes declare what they read,
the graph resolves order and target lifetimes, and the pool recycles
framebuffers. A six-effect chain runs on two targets and allocates nothing
after the first frame.

**`quad_renderer.hpp`** — the textured quad everything above is built from, and
the final linear-to-sRGB pass. Nothing it takes is in pixels: sizes are
fractions of the target's smaller dimension, so a scene authored in a small
preview panel exports at 4K unchanged.

## Testing

Tests that need a GL context create a hidden window and report themselves
skipped when there is no display, so they are useful locally and harmless on
CI. The preprocessor, the frame graph and the pool are all testable headless,
which is deliberate — those are where the logic lives.
