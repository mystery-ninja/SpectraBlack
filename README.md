# Spectra Black

A motion-graphics and music-visualization tool that merges the two halves nobody
has put together properly yet:

- **From Avee Player:** real-time, audio-reactive rendering. Properties that are
  driven live by the music instead of by hand.
- **From After Effects:** a real timeline with keyframes, nested compositions,
  a layer stack, and a deterministic offline render.

The thesis is simple. Avee can make something react to a beat but cannot make it
happen at 1:04.5. After Effects can make something happen at exactly 1:04.5 but
has no idea what the music is doing. Spectra Black is the tool where a single
property can do both at once.

The name: a **spectrum** is both a band of light and the frequency content of
audio, which is exactly the intersection this app occupies. **Black** is the
suffix for *complete* — in the sense of a black belt, and of the empty canvas
the visuals emerge from.

> **Status: Stages 1 and 2 complete.** The documentation and architecture were
> written first, so that the people joining this project build toward the same
> thing. The headless core — properties, keyframes, audio drivers, the node
> tree, save/load and undo — exists and is tested, and so does the renderer:
> an OpenGL 4.3 RHI, a frame graph with pooled render targets, and linear-light
> compositing. Next is the audio engine, and then Stage 4 joins the three.
> See [`docs/15-BUILD-ORDER.md`](docs/15-BUILD-ORDER.md).

## Targets

| Platform | Role | Priority |
|---|---|---|
| Linux (x86_64) | Full editor | Phase 1 |
| Windows (x86_64) | Full editor | Phase 1 |
| Android (arm64) | Player, preview, light editing | Phase 3 |

Apple platforms are explicitly out of scope. That removes any need for a Metal
backend and is a deliberate scope cut, not an oversight.

## Stack at a glance

C++20 core, OpenGL 4.3 on desktop and OpenGL ES 3.2 on Android behind one thin
rendering abstraction, FFmpeg for decode and encode, Dear ImGui for the desktop
editor. The reasoning behind each of those is in
[`docs/02-TECH-STACK.md`](docs/02-TECH-STACK.md).

## Building

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Requires CMake 3.24+, Ninja, and a C++20 compiler (GCC 13+, Clang 16+, or
MSVC 19.3x). Dependencies are fetched automatically.

```bash
sudo apt install cmake ninja-build clang-format clang-tidy
```

The GPU, audio, and editor modules are **off by default** so a fresh clone
builds in seconds. Turn them on as the matching stage of
[`docs/15-BUILD-ORDER.md`](docs/15-BUILD-ORDER.md) begins:

| Option | Default | Enables |
|---|---|---|
| `SPECTRA_ENABLE_GPU` | `OFF` | The renderer; fetches GLFW, glad and stb (Stage 2) |
| `SPECTRA_ENABLE_AUDIO` | `OFF` | Decode, playback, analysis; needs FFmpeg (Stage 3) |
| `SPECTRA_ENABLE_APP` | `OFF` | The desktop editor (Stage 5) |
| `SPECTRA_BUILD_TESTS` | `ON` | Unit tests |

Other presets: `linux-asan` (ASan + UBSan), `linux-release`, `linux-ci`
(warnings as errors), `linux-gpu` (strict, with the renderer), `windows-debug`,
`windows-release`.

The renderer needs the X11 development packages from
[`docs/02-TECH-STACK.md`](docs/02-TECH-STACK.md), and Python with Jinja2 for
glad, which generates the GL loader at configure time:

```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev \
                 libxcursor-dev libxi-dev python3-jinja2
cmake --preset linux-gpu && cmake --build --preset linux-gpu
./build/linux-gpu/bin/spectra_gpu_demo
```

Before pushing, run the two checks CI runs:

```bash
./scripts/check-core-deps.sh                        # architecture guard
clang-format -i $(git ls-files '*.cpp' '*.hpp')     # formatting
```

## Read the docs in this order

| Doc | What it covers |
|---|---|
| [00-VISION.md](docs/00-VISION.md) | What we are building and the open question of how it should *feel*. **Start here.** |
| [01-ARCHITECTURE.md](docs/01-ARCHITECTURE.md) | Module layering and how data flows through a frame |
| [02-TECH-STACK.md](docs/02-TECH-STACK.md) | Language, libraries, build system, and why |
| [03-RENDER-ENGINE.md](docs/03-RENDER-ENGINE.md) | The GPU pipeline, and how preview and export stay identical |
| [04-AUDIO-ENGINE.md](docs/04-AUDIO-ENGINE.md) | Decoding, FFT analysis, beat detection, reactive signals |
| [05-ANIMATION-MODEL.md](docs/05-ANIMATION-MODEL.md) | The core idea: keyframes and audio drivers on one property |
| [06-PROJECT-FORMAT.md](docs/06-PROJECT-FORMAT.md) | How a project is saved, shared, and diffed |
| [07-UI-UX.md](docs/07-UI-UX.md) | Panel layout, timeline design, interaction |
| [08-ROADMAP.md](docs/08-ROADMAP.md) | Phases, milestones, and what "done" means for each |
| [09-AVEE-TEARDOWN.md](docs/09-AVEE-TEARDOWN.md) | What we learned from the Avee Player source, and what to copy |
| [10-GIT-GUIDE.md](docs/10-GIT-GUIDE.md) | Git from scratch, plus our branching and review workflow |
| [11-NAMING-AND-LICENSE.md](docs/11-NAMING-AND-LICENSE.md) | Name candidates and the license decision, with tradeoffs |
| [12-AEP-COMPATIBILITY.md](docs/12-AEP-COMPATIBILITY.md) | Scoped import of After Effects projects, the fidelity limits, and the zipped-archive path |
| [13-EXTENSIBILITY.md](docs/13-EXTENSIBILITY.md) | Property expressions, scriptable effect nodes, and editor extensions — with the determinism and sandbox rules |
| [14-VIZ-COMPATIBILITY.md](docs/14-VIZ-COMPATIBILITY.md) | Import of Avee `.viz` projects — easier than the AE path and aimed at our beachhead audience |
| [15-BUILD-ORDER.md](docs/15-BUILD-ORDER.md) | **Picking up a task? Start here.** What to build in what order, and who can work in parallel |
| [16-PLUGIN-HOSTING.md](docs/16-PLUGIN-HOSTING.md) | Can we run AE's `.aex` plugins? The platform blocker, and OpenFX as the open alternative |
| [17-FEATURE-PARITY.md](docs/17-FEATURE-PARITY.md) | **Every capability from both parent apps and its status.** Nothing gets dropped without a written reason |

### References (read any time)

| Doc | What it covers |
|---|---|
| [GIT-CHEATSHEET.md](docs/GIT-CHEATSHEET.md) | **New to git? Start here.** Setup, the commands that matter, and what to watch out for |

## Team

| | Role |
|---|---|
| **MysterY** ([@mystery-ninja](https://github.com/mystery-ninja)) | Project lead |
| **Schxma** ([@tlgkxd123](https://github.com/tlgkxd123)) | Contributor |
| **Zion** | Contributor |
| **Ayashii** | Contributor |

We fail together and we succeed together. Everyone here has push access and the
workflow is flat by design — see [`CONTRIBUTING.md`](CONTRIBUTING.md).

Want in? Open an issue or start a [discussion](https://github.com/mystery-ninja/SpectraBlack/discussions).

## Reference material

Neither of these ships with the project. They are research material only, held
locally and never committed.

- **Avee Player 1.2.101**, decompiled to readable Java. Our primary architectural
  reference; the teardown is in
  [`docs/09-AVEE-TEARDOWN.md`](docs/09-AVEE-TEARDOWN.md).
- **After Effects** under a Wine prefix. Binaries only, so it is a reference for
  *interaction design* and nothing more.

## License

**GPL-3.0** — see [`LICENSE`](LICENSE).

You may use, modify, and redistribute this freely. If you distribute a modified
version you must credit the original authors and release your source under the
same license. Nobody can take this work closed-source, which is the entire
reason for the choice. The reasoning is in
[`docs/11-NAMING-AND-LICENSE.md`](docs/11-NAMING-AND-LICENSE.md).

## Provenance

Avee RE is decompiled third-party code. We read it to understand *techniques*
and we write our own implementations of those techniques. We do not copy its
code into this repository. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the rule
and the reasoning.
