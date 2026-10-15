# 02 — Tech Stack

## Language: C++20

Your call, and the right one for this app. Live preview and fast export are the
whole value proposition, and a garbage-collected language fighting you at 60fps
over a 4K framebuffer is not a trade worth making. C++ is also the only mainstream
option that runs natively on Windows, Linux, and Android via the NDK with one
codebase.

We target **C++20**, not 23. GCC 13 (what you have) and MSVC 19.3x both have
solid C++20 support, and the Android NDK's Clang does too. C++23 library support
is still patchy across those three compilers, and a public project should not be
making contributors chase toolchain versions.

What we actually use from C++20, in rough order of value: designated
initializers, `std::span`, concepts for the property system, `<bit>`, and
`constexpr` math. We are explicitly **not** using coroutines or modules — module
support across GCC/MSVC/NDK is still not worth the build-system pain.

### House rules

- No exceptions in `core/` or on the audio path. `std::expected`-style result
  types instead (hand-rolled for C++20).
- No RTTI. Node types get an enum tag and a visitor.
- No raw `new`/`delete`. `unique_ptr` for ownership, raw pointers for non-owning
  views.
- No singletons. Context objects passed explicitly.

## Build: CMake + Ninja

**You do not have CMake installed yet.** Install before Phase 1:

```bash
sudo apt install cmake ninja-build
```

CMake is not a joy, but it is the only build system where GLFW, FFmpeg, ImGui,
the Android NDK, and MSVC all work without custom glue. Ninja because it is
dramatically faster than Make on 12 cores.

Dependencies come in through `FetchContent` rather than being vendored by hand or
requiring `vcpkg`. A contributor should get a working build from `git clone` plus
`cmake --preset linux-debug`, with no package manager setup. The one exception is
FFmpeg, which is a genuine pain to build from source — on Linux and Android we
link the system or NDK-prebuilt libraries, and on Windows we fetch the
pre-built shared binaries.

## Graphics: OpenGL 4.3 core / OpenGL ES 3.2

The pragmatic choice, because GL 4.3 and GLES 3.2 are close to a strict feature
superset relationship. Both give us compute shaders, uniform buffer objects,
instanced rendering, and texture arrays. That means **one renderer, one set of
shaders**, with a preprocessor shim handling the `#version` line and a handful
of precision qualifiers.

Your RTX 2070 reports GL 4.6, so desktop has headroom to spare. GLES 3.2 covers
roughly 70% of active Android devices, which is acceptable for a Phase 3 target
aimed at enthusiasts.

### Why not the alternatives

| Option | Verdict |
|---|---|
| **Vulkan** | Genuinely faster for heavy workloads, but three to five times the code for every feature, and we are fill-rate bound rather than draw-call bound. Wrong bottleneck to optimize. Keep the RHI clean so it stays possible. |
| **D3D11/12** | Would mean a second renderer for Windows alone. GL works fine on Windows with vendor drivers. |
| **bgfx** | Tempting — it wraps GL, GLES, D3D, Vulkan, and Metal. But it imposes its own shader language and resource model, and debugging a performance problem through someone else's abstraction is miserable. Reconsider if we ever need D3D12. |
| **WebGPU (Dawn/wgpu)** | Interesting future-proofing and would enable a web demo, but the C++ API is still moving and it adds a large dependency. Revisit in 2027. |

**Decision: own thin RHI over GL, structured so a second backend is additive.**

## Library choices

| Need | Pick | Why |
|---|---|---|
| Window + input (desktop) | **GLFW 3.4** | Small, battle-tested, Win+Linux, no opinions about your app |
| GL function loading | **glad 2** | Generated loader, no runtime dependency |
| Math | **GLM** | GLSL-shaped types, header-only, familiar to any graphics contributor |
| Editor UI | **Dear ImGui** (docking branch) | GPU-native, renders in our own context, ideal for dense tool UIs |
| Audio playback | **miniaudio** | Single header, and it covers WASAPI, ALSA/PulseAudio, *and* AAudio — one library for all three targets |
| Audio/video decode + encode | **FFmpeg** (libav*) | Nothing else decodes everything users will throw at it. Already on your box at 6.1.1 |
| FFT | **PFFFT** | Small, SIMD, real-input optimized, BSD-ish. KissFFT if we need simpler |
| Image loading | **stb_image** | Single header, handles what matters |
| Text shaping | **FreeType + HarfBuzz** | Needed for real typography. Avee's sprite-font approach cannot do kerning, ligatures, or non-Latin scripts |
| JSON | **nlohmann/json** | Ergonomics over speed; project files are small and load once |
| Tests | **doctest** | Faster to compile than Catch2, same ergonomics |
| GPU video encode | **NVENC** via FFmpeg | Your 2070 has a good encoder; makes 4K export dramatically faster |

### On text rendering

Worth calling out because it is a trap. Avee uses `SpriteFont` with a texture
atlas, which is fine for `{Artist} - {Title}` and falls apart for anything a
motion designer wants. Doing text properly means FreeType for rasterization and
HarfBuzz for shaping, and it is a meaningful chunk of work. It is scheduled in
Phase 2 rather than Phase 1 for that reason, with a basic atlas renderer first.

## Platform matrix

| | Linux | Windows | Android |
|---|---|---|---|
| Compiler | GCC 13+ / Clang 16+ | MSVC 19.3x | NDK Clang (r26+) |
| Graphics | GL 4.3 core | GL 4.3 core | GLES 3.2 |
| Windowing | GLFW (X11 + Wayland) | GLFW (Win32) | `native_app_glue` |
| Audio out | ALSA / PulseAudio | WASAPI | AAudio |
| UI | Dear ImGui | Dear ImGui | Compose (Kotlin) + JNI |
| Packaging | AppImage + Flatpak | Portable zip + installer | APK / AAB |

Note the Android UI divergence. ImGui is genuinely bad for touch — it assumes a
precise cursor and hover states that do not exist on a phone. Android therefore
gets a native Compose UI talking to the same C++ core over JNI. That is real
extra work and is the main reason Android is Phase 3 rather than Phase 1.

## Development environment

You already have: GCC 13.3, Java 25, Node 24, Python 3.12, FFmpeg 6.1.1, Git
2.43, an RTX 2070 with GL 4.6, 12 cores, 15 GB RAM. That is a comfortable
machine for this.

Still to install, in order of when you will need it:

```bash
# Phase 1, immediately
sudo apt install cmake ninja-build clang-format clang-tidy

# Phase 1, for GLFW to build against
sudo apt install libx11-dev libxrandr-dev libxinerama-dev \
                 libxcursor-dev libxi-dev libwayland-dev

# Phase 1, FFmpeg development headers
sudo apt install libavcodec-dev libavformat-dev libavutil-dev \
                 libswscale-dev libswresample-dev

# Phase 2, text rendering
sudo apt install libfreetype-dev libharfbuzz-dev

# Phase 3, Android (you already have the SDK under ~/Android)
# install NDK r26+ via sdkmanager
```

A 15 GB RAM machine with 12 cores will want `-j10` rather than `-j12` for
parallel builds, since C++ template instantiation is memory-hungry and swapping
costs more than the two extra jobs gain.

## Code quality tooling

- **clang-format** with a checked-in `.clang-format`, enforced in CI. Formatting
  arguments on a public repo are a waste of everyone's time; let the tool decide.
- **clang-tidy** for the obvious bug classes, warnings as errors in CI.
- **ASan + UBSan** on the Linux debug CI job. In a C++ graphics project these
  catch more real bugs than any amount of code review.
- **RenderDoc** for GPU debugging. Non-negotiable for a renderer; it is how you
  find out what the GPU actually did instead of guessing.

## CI

GitHub Actions, three jobs to start:

1. **Linux** — GCC 13, Debug + ASan/UBSan, run `ctest`
2. **Windows** — MSVC, Release, build only at first
3. **Lint** — clang-format check, clang-tidy, and the `core/` dependency check
   that fails if anything in `core/` includes GL, OS, or UI headers

The third job is the one that actually protects the architecture. Everything
else is conventional.
