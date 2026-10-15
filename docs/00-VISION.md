# 00 — Vision

## The gap we are filling

Put the two reference apps side by side and the hole in the market is obvious.

|                         | Avee Player                  | After Effects                           |
|-------------------------|------------------------------|-----------------------------------------|
| Reacts to audio         | Yes, natively and live       | Partly, only via clumsy keyframe baking |
| Timeline with keyframes | **No**                       | Yes, excellent                          |
| Nested compositions     | Yes (`Composition` elements) | Yes, excellent                          |
| Preview speed           | Instant, 60fps, GPU          | Slow, constant RAM previews and caching |
| Effects                 | 26 element types             | 369 bundled plugins                     |
| Precise timing control  | **No**                       | Yes                                     |
| Text engine             | Basic sprite fonts           | Full typographic control                |
| Platform                | Android only                 | Windows/macOS only                      |

Avee's genuine innovation is that **a property can be wired to the music.** Its
`MeasuredVar` type means position, scale, or rotation is not a number, it is a
rule: "be the beat, scaled by 0.6." The visualizer is therefore alive without
anyone animating anything.

Avee's fatal limit is that this is *all* it can do. There is no concept of time
beyond "now." You cannot say "the logo flies in during the intro and leaves at
the drop," because there is no timeline to say it on.

After Effects is the mirror image. Total control over time, zero awareness of
sound. Its audio amplitude keyframes are a workaround, not a feature: you bake
amplitude to keyframes, and the moment you change the track you redo everything.
At least After Effect has plugins that extends the app and makes creation easier
without having to rely on the normal way of doing things.

**Spectra Black's core claim: a property should accept both a keyframe curve and an
audio driver at the same time, and blend them.** That is the feature neither
parent has, and everything else in this project exists to support it.

## Nothing goes missing by accident

We are combining two apps, so the default answer for any capability either
parent has is **we keep it**. A feature may be left out only as a decision that
somebody wrote down, with a reason. It may never be left out because it was
overlooked, because nobody got round to it, or because it looked hard.

That distinction is the whole point. An Avee user arriving here must not find
that the one effect they built their style around silently vanished, and an AE
user must not discover halfway through a project that a basic part of the
workflow was never considered.

The rule is enforced by a ledger rather than by good intentions:
[`17-FEATURE-PARITY.md`](17-FEATURE-PARITY.md) lists every capability from both
parents and its current status. Dropping something means changing its status to
`Dropped` and writing why — the row stays, so the question "why doesn't Spectra
Black have X?" always has an answer.

This is not the same as promising feature-for-feature parity with After Effects.
AE ships 369 plugins, many of them film-finishing tools with no bearing on music
visuals, and the ledger marks those out of scope explicitly. The promise is that
the decision was *made*, not that everything was built.

## The identity question

You asked to settle this in the docs rather than up front, so here is the real
decision laid out. The question is not which features we build — it is which
one is the *skeleton* and which one is grafted on, because that determines the
data model, and the data model is almost impossible to change later.

### Option A — After Effects skeleton, audio grafted on

Time is the organizing principle. A project has a fixed duration, a composition
has a frame rate, every layer has an in/out point, and the timeline is the main
panel. Audio reactivity becomes a new *kind of keyframe interpolation*.

- **Good:** Anyone who has used AE, Premiere, Blender, or DaVinci is instantly
  productive. Precise timing is natural. Export is trivially deterministic.
- **Bad:** Live preview becomes a secondary mode, and Avee's "it just reacts,
  with no work from me" magic is at risk of being buried under timeline UI.

### Option B — Avee skeleton, timeline grafted on

The element tree is the organizing principle, exactly like Avee. A scene is a
living thing that responds to whatever audio you feed it. The timeline is an
optional overlay for scheduling.

- **Good:** Preserves the thing that makes Avee special. A scene stays reusable
  across any track, which is why Avee presets are shareable at all.
- **Bad:** Grafting real time onto a timeless model is historically where tools
  get ugly. Precise work fights the grain of the tool.

### Option C — Dual-nature from the ground up (recommended)

Treat "track-relative time" and "audio-reactive signal" as two peer inputs to one
evaluation system, and design the data model so neither is privileged.

Concretely, every animatable property evaluates as:

```
value(t) = base(t) ⊕ Σ driver_i(audio, t) * weight_i
```

where `base(t)` is a keyframe curve (possibly just a constant) and each `driver`
is an audio-reactive signal with its own blend operator (add, multiply, replace,
max). A property with no drivers behaves exactly like After Effects. A property
with no keyframes behaves exactly like Avee. Most interesting properties have
both, and that is the point.

This costs more design work now and almost nothing in runtime complexity, since
both paths collapse into the same evaluator. It is also the only option where
the headline feature is structural rather than bolted on.

**My recommendation is Option C**, with the *default UI posture* leaning Avee:
when you open the app and drop in a track, things should already be moving. The
timeline is there when you want precision, not a wall you must climb first.

There is a decision to confirm at the end of this document.

## Who this is for

1. **The Avee power user** who has hit the ceiling. They know what
   `BeatCamShakeMore` does and they want keyframes and a 4K export.
2. **The AE user doing music videos** who is tired of baking audio keyframes and
   wants a preview that does not take 40 seconds per change.
3. **The streamer or small creator** who wants good-looking audio visuals
   without learning a compositor.

Group 1 is the beachhead. They are underserved, they are already in communities
we can reach, and they will tolerate rough edges in exchange for the ceiling
being lifted.

## Non-goals

Writing these down now prevents years of scope argument.

- **Not a video editor.** No multi-clip NLE timeline, no transitions between
  cuts. Feed it a track and make visuals.
- **Not a 3D program.** Flat layers in a 2.5D space with Z ordering and a
  camera, like After Effects. No meshes, no lighting, no raytracing.
- **Not a DAW.** We read audio, we never write it.
- **No Apple platforms.** Stated once, applies forever.
- **Not an AE plugin host.** No AEX compatibility, ever. It is a reverse
  engineering nightmare and a legal one.
- **Not cloud anything** in the foreseeable future. It is a local tool.

## What success looks like

Phase 1 ships when someone can drop in an MP3, build a scene with reactive bars
and a logo that keyframes in during the intro, and export a 1080p60 MP4 that
matches the preview frame for frame. If that works and feels fast, the concept
is proven and everything after is content.

## Naming

**Spectra Black.**

"Black" is a suffix meaning *complete* — every feature present, well designed,
nothing missing, in the sense of a black belt rather than naive perfection. It
also happens to describe what a visualizer renders onto: an empty composition is
black, and light comes out of it.

"Spectra" carries both halves of the app in one word. A spectrum is a band of
**light** and it is the frequency content of **audio**, and "spectrum analyzer"
is literally what the core of the renderer is. Both meanings are correct, which
makes it a description rather than a pun.

Alternatives considered and the reasoning are in
[`11-NAMING-AND-LICENSE.md`](11-NAMING-AND-LICENSE.md).

## Open decisions

These block nothing today but should be closed before Phase 1 code begins.

1. **Confirm the animation model.** Option C above is the recommendation, but
   this is deliberately left open so contributors can weigh in. It shapes the
   core data structures and is the hardest decision to revisit later, so it is
   the most valuable thing in these docs to argue about right now.
2. **Pick a name** before the repo is made public.
3. **Pick a license**, before any outside contributions are accepted. Relicensing
   later needs permission from every contributor.
4. **Decide whether Android is an editor or a player.** Affects whether the core
   needs a touch-first UI abstraction. Current plan assumes player-first.

Items 2 and 3 are analyzed in
[`11-NAMING-AND-LICENSE.md`](11-NAMING-AND-LICENSE.md).
