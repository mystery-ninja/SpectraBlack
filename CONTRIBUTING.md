# Contributing

Thanks for wanting to help. This project is in the planning stage, so right now
the most valuable contribution is argument: read [`docs/00-VISION.md`](docs/00-VISION.md)
and tell us where it is wrong.

## Before you write code

Read, in order:

1. [`docs/00-VISION.md`](docs/00-VISION.md) — what we are building and why
2. [`docs/01-ARCHITECTURE.md`](docs/01-ARCHITECTURE.md) — the module layering
3. [`docs/02-TECH-STACK.md`](docs/02-TECH-STACK.md) — how to build it

If you are new to git or want our specific workflow, see
[`docs/10-GIT-GUIDE.md`](docs/10-GIT-GUIDE.md).

## The rule about Avee Player source

This project was designed by studying a decompiled copy of Avee Player. That
study produced [`docs/09-AVEE-TEARDOWN.md`](docs/09-AVEE-TEARDOWN.md), which
describes *techniques and architecture*.

**Do not copy Avee code into this repository.** Not a function, not a shader, not
a constant table. Decompiled code is derivative of a proprietary work and we have
no license to it. Copying it would make this project legally unredistributable
and would poison the repository in a way that is very hard to undo later.

Reading it to understand how a Hann window is applied, or that smoothing across
neighboring FFT bins makes bars look better, is fine — those are general
techniques, documented in public DSP literature, and not anyone's property. Write
your own implementation.

The same applies to anything extracted from After Effects.

If you are not sure which side of the line something falls on, open an issue and
ask before writing it.

## Code standards

Full detail in [`docs/02-TECH-STACK.md`](docs/02-TECH-STACK.md). The short
version:

- **C++20.** No coroutines, no modules.
- **`core/` has no dependencies** on GL, OS APIs, or UI. CI enforces this.
- **No exceptions** in `core/` or on the audio path; use result types.
- **No RTTI**, no singletons, no raw `new`/`delete`.
- **`clang-format` decides formatting.** The config is checked in; run it before
  committing and do not argue with it.
- **Warnings are errors** in CI.

### Naming

```cpp
class  SpectrumBarsNode;     // PascalCase types
void   evaluateProperty();   // camelCase functions
float  barCount;             // camelCase members
constexpr int kMaxBands;     // k-prefixed constants
#define NEWAPP_SOMETHING     // SCREAMING_CASE macros, prefixed
```

## Tests

- Anything in `core/` or `audio/` needs unit tests. Both are pure logic with no
  excuse for being untested.
- Renderer changes need a golden image test if they affect output.
- Any bug fix should come with a test that fails without the fix. This is the
  single highest-value testing rule and we hold to it.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## How changes land

The workflow is **flat and trust-based**. If you have push access you can commit
and branch freely, including on `main`. There is no mandatory review. See
[`docs/10-GIT-GUIDE.md`](docs/10-GIT-GUIDE.md) for the mechanics.

Use a branch and open a pull request when you *want* feedback, when a change is
large, or when it touches something someone else is working on. Not because a
rule says so.

When you do open one:

- One logical change per PR. A PR that fixes a bug and refactors a module is two
  PRs.
- Describe **why**, not just what. The diff shows what.
- Include before/after screenshots or a clip for anything visual.
- Note any change to the project file format explicitly, since those need a
  migration — see [`docs/06-PROJECT-FORMAT.md`](docs/06-PROJECT-FORMAT.md).

Reviews are about the code. Nobody here is their pull request.

The one hard rule: **never force-push a shared branch.** Everything else in git
is recoverable; that is the single action that destroys other people's work.

## Performance claims

This project lives or dies on being fast, so performance claims need numbers.
"This should be faster" is not a justification; a before-and-after frame time on
a stated scene is. The budget in
[`docs/03-RENDER-ENGINE.md`](docs/03-RENDER-ENGINE.md) is the reference.

Equally: do not optimize something that is not in a profile. Most of this
codebase does not need to be fast, and complexity spent in the wrong place is
worse than no optimization at all.

## Licensing

This project is **GPL-3.0**. See [`LICENSE`](LICENSE), and
[`docs/11-NAMING-AND-LICENSE.md`](docs/11-NAMING-AND-LICENSE.md) for why.

What it means for you as a contributor:

- You keep copyright over your own contributions. There is no CLA and no
  copyright assignment, which means nobody — including the project founder — can
  relicense your work without asking you.
- By contributing, you agree your work is released under GPL-3.0.
- Anything you add must be GPL-compatible. Do not add a dependency under a
  proprietary or incompatible license; if you are unsure, ask in an issue first.
- Attribution is mandatory for anyone who redistributes, and nobody can take this
  work closed-source. That is the point.

## Where to start

Once Phase 1 begins, issues will be labeled:

- `good-first-issue` — self-contained, well-specified
- `needs-design` — the approach is not settled; discuss before coding
- `core`, `gpu`, `audio`, `render`, `ui` — by module

Right now, in Phase 0, the open questions at the bottom of
[`docs/00-VISION.md`](docs/00-VISION.md) are where help is most useful.
