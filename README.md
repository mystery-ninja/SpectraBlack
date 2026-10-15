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

> **Status: planning only.** There is no code yet, by design. We are writing the
> documentation and the architecture down first so that the people joining this
> project are building toward the same thing.

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

## Reference material on this machine

Neither of these ships with the project. They are research material only.

- `/media/mystery/MysterYFile/Avee Player/avee-re-master` — decompiled Avee
  Player 1.2.101, readable Java. This is our primary architectural reference.
- `~/AeNux` — After Effects under a bundled Wine prefix. Binaries only, so it is
  a reference for *interaction design*, nothing more.

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
