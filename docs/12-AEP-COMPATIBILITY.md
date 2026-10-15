# 12 — After Effects Project Import (`.aep` / `.aepx` / `.zip`)

## What this is, and what it is not

"`.aep` compatibility" is the kind of phrase that sinks a project if it is taken
literally, so the first job of this document is to redefine it into something
buildable.

**We are planning one-way import of a scoped subset of an After Effects project,
not fidelity.** You will be able to take an AE project, bring its compositions,
layers, transforms, keyframes, nesting, and markers into Spectra Black, and get a
report of everything that did not survive. You will not get AE's effects, its
expression engine, its 3D space, or pixel-identical output, because those either
do not exist in our model or cannot be reproduced without AE itself.

This is the same honesty posture as the rest of the docs: a scoped feature that
actually ships beats a total one that never does.

### Reconciling with the non-goals

[`00-VISION.md`](00-VISION.md) says, flatly: *"Not an AE plugin host. No AEX
compatibility, ever."* That still holds and this document does not touch it.

- **AEX** is a compiled C++ plugin binary that runs *inside* AE against Adobe's
  private ABI. Hosting those is a reverse-engineering and legal nightmare, and we
  will never do it.
- **`.aep`** is a *data file* the user authored and owns. Reading a user's own
  project file is a different proposition — legally ordinary, technically bounded.

So: we host no Adobe code and we parse no plugin binaries. We read a file a user
exports, and we map what we understand onto our own data model.

## The format reality

There are three on-disk shapes this feature can consume, in descending order of
how painful they are to read.

| Form | What it is | Parse difficulty | Needs AE to produce |
|---|---|---|---|
| `.aep` | Native binary, a RIFX (big-endian RIFF) chunk tree. Undocumented. | High — reverse engineering | No (it is what AE saves by default) |
| `.aepx` | Official XML serialization of the same project (AE: *Save a Copy As XML*). | Medium — tractable, but the schema is undocumented and version-fragile | Yes (you need AE to write it) |
| Lottie JSON | `bodymovin` plugin export. An open-ish, documented JSON schema. | Low | Yes (needs the Bodymovin plugin) |

The key realization that sets our priorities: **anyone who has a `.aep` has After
Effects** — that is how the file came to exist. So the "user has a `.aep` but no
AE" case is rare, and we do not need to prioritize parsing the raw binary to serve
it. That single observation lets us avoid the worst of the work.

## The fidelity contract

Every import is lossy, so the contract is about being *predictable* about the
loss. Each AE concept lands in exactly one of three buckets, and the importer
tells the user which.

### Imports exactly

| AE concept | Spectra Black target | Notes |
|---|---|---|
| Composition (size, fps, duration) | `Composition` | Direct |
| Solid layer | `SolidColor` node | Direct |
| Still-image / footage-as-image layer | `Image` node | Asset copied/relinked; see `.zip` section |
| Null layer | empty `Group` | Used as a parenting anchor |
| Nested composition / precomp | nested `Composition` | A Phase 2 feature on our side |
| Transform: Position, Scale, Rotation, Opacity | `Property<T>` + keyframes | With unit conversion, below |
| Keyframe interpolation: Linear, Bezier, Hold | our `Interpolation` + bezier handles | AE influence/ease maps to our handles |
| Composition / layer markers | timeline markers | Comment becomes the marker label |

### Imports approximately (with a warning)

| AE concept | Mapped to | Why it is approximate |
|---|---|---|
| Anchor point (arbitrary px) | our `Anchor` + offset | We model fewer anchor degrees of freedom |
| Parenting hierarchy | `Group` nesting | AE parenting is transform inheritance; close but not identical |
| Blend modes | nearest of our set | See mapping below; unmapped modes snap to the closest + warn |
| Spatial motion path | Phase 2 motion paths | Dropped until motion paths land ([`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md)) |
| Text layer | basic `Text` node | String + transform only; per-character animators dropped. Depends on Phase 2 text |
| Audio layer | our single audio source | We ignore AE's audio-amplitude keyframes and offer live drivers instead — see below |

### Dropped (preserved in the report, not silently)

- All effects except a small hand-mapped set (see "Effects", below)
- Expressions that cannot be baked
- 3D layers, cameras, lights (we are 2.5D by design — a non-goal)
- Masks and track mattes (our Phase 4, per the roadmap)
- Time remapping, motion blur settings, layer styles

A dropped layer does not vanish. Mirroring the rule for unknown node types in
[`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md), it becomes a **visible placeholder
node** that preserves its raw AE parameters verbatim, so nothing is lost silently
and a future importer version can do better with the same file.

## Value and coordinate conversion

AE and Spectra Black disagree on units, and getting this wrong is the
"it looked right in AE" class of bug, so it is specified rather than left to the
implementation.

| Quantity | AE | Spectra Black | Conversion |
|---|---|---|---|
| Position | pixels, comp space, origin top-left, Y-down | normalized to the smaller dimension ([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md)) | divide by `min(compW, compH)`; map origin/Y to our canvas convention |
| Scale | percent (100 = full) | factor (1.0 = full) | `/ 100` |
| Rotation | degrees | degrees | direct |
| Opacity | 0–100 | 0–1 | `/ 100` |
| Time | seconds (or frames) | `double` seconds | direct; resolve frames via comp fps |

The position conversion is the load-bearing one. Because our spatial model is
resolution-independent and AE's is pixel-based, the importer must know the comp
resolution to normalize, and must pin down our canvas origin convention (center
vs top-left) so round-trips are predictable. That convention should be stated once
in [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md) and referenced here, not invented
in the importer.

### Blend mode mapping

Our six modes are in [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md). Phase 1 mapping:

| AE | Spectra Black |
|---|---|
| Normal | `Alpha` |
| Add / Linear Dodge | `Add` |
| Screen | `Screen` |
| (Multiply, Overlay, Soft Light, Difference) | nearest + warn until the Phase 2 Photoshop set exists |
| anything else | `Alpha` + warn |

### Effects

Almost everything in AE's effect catalog has no home here and is dropped with a
report entry. A **small** hand-mapped set is worth doing because the targets
already exist in our roadmap:

| AE effect | Spectra Black node | Phase |
|---|---|---|
| Gaussian / Fast Blur | `BlurNode` | 2 |
| Tint / Fill / Lumetri-basic | `ColorGradeNode` | 2 |
| (future) RGB split-style | `RgbSplitNode` | 2 |

Everything outside that table is dropped. We explicitly do **not** try to emulate
AE effects; that path has no bottom.

## Expressions → bake, never execute

AE expressions are a full ECMAScript engine. We will not ship a JS runtime on the
render path (see the expression discussion in
[`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md)), and even if we did, executing
arbitrary AE expressions outside AE is not feasible.

The resolution fits our architecture rather than fighting it: **bake expressions
to keyframes at import.** Our entire correctness model already requires every
property to be a deterministic, pure function of time
([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md),
[`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)), and we already have a "bake drivers
to keyframes" operation in the animation model. Baking is only possible when AE
itself can sample the expression, which is why the AE-side exporter route below is
the high-fidelity path.

There is a genuinely nice outcome hiding here. AE's standard trick for audio
reactivity is to convert *Audio Amplitude* to baked keyframes and reference them
in expressions — the exact workaround [`00-VISION.md`](00-VISION.md) is built to
replace. On import we can **recognize that pattern, discard the baked amplitude
keyframes, and offer to reconnect the property to a live audio driver**. The
import does not just preserve the AE project; it upgrades it into the thing this
app exists to be.

## Routes to get the data in

Four ways to cross the gap, presented with a recommendation in the house style.

### Route A — Parse `.aepx` (XML) directly *(recommended primary)*

The user saves their project as XML from AE; we parse the subset.

- **Good:** no binary reverse engineering; XML is tractable; reading the user's
  own file is legally clean; we need no AE runtime of our own.
- **Bad:** the schema is undocumented and drifts across AE versions; requires the
  user to do a Save-As step.

### Route B — Parse binary `.aep` (RIFX) directly *(revised: viable, now recommended)*

**This assessment was wrong when first written and is revised here.** The original
reasoning — undocumented binary, brittle reverse engineering, park it behind
demand — assumed we would be doing the reverse engineering ourselves. We would
not. The community already did it, and the work is maintained, permissively
licensed, and current.

| Project | Language | License | State |
|---|---|---|---|
| [`forticheprod/py-aep`](https://github.com/forticheprod/py-aep) | Python | MIT | **Actively maintained**, handles AE 2026 project stamps |
| [`boltframe/aftereffects-aep-parser`](https://github.com/boltframe/aftereffects-aep-parser) | Go | MIT | Dormant since 2022, but documents the RIFX chunk types |

`py-aep` reads and writes binary `.aep` **with no After Effects installed**,
exposing compositions, layers, properties, keyframes, text, shapes, and gradients
through an ExtendScript-shaped API. It explicitly does not evaluate expressions
and does not render — which is exactly the boundary we also draw.

What this changes:

- **The format is not a mystery.** Two independent MIT implementations document
  the RIFX chunk layout, so our importer is a porting job against a known
  structure rather than a reverse-engineering spike.
- **MIT is GPL-compatible**, so we can read, port, and adapt the format knowledge
  and the parsing logic directly. We are not reverse engineering Adobe's binary;
  we are reimplementing a documented open-source parser.
- **No AE required**, which removes the Save-As step that Route A imposes and
  serves users whose only artifact is the `.aep` itself.

- **Good:** users drop their native file, no extra step, no AE needed; prior art
  is permissive and current.
- **Bad:** still version-fragile — the chunk layout shifts across AE releases, and
  that maintenance cost is recurring. Expressions still cannot be evaluated, so
  Route D remains the only path for those.

### Why the recommendation changed

The earlier logic was *"anyone with a `.aep` has After Effects, so Route A is
enough."* That holds for a user importing their own work. It does not hold for the
case that actually matters to the AE community: **receiving a project file from
someone else.** Collaborators, template marketplaces, and tutorial project files
all circulate as `.aep`, and the recipient frequently cannot or will not open AE
to re-save it as XML. Requiring a Save-As step makes the feature useless at
exactly the moment it is most valuable.

### Route C — Lottie / Bodymovin JSON *(separate, orthogonal win)*

Consume Lottie JSON exported by the Bodymovin plugin.

- **Good:** Lottie is an open, documented-ish schema with a large ecosystem well
  beyond AE; maps cleanly to keyframed transforms and shape animation; no RE.
- **Bad:** Lottie is itself a subset of AE and is vector-oriented, so it is not a
  general AE importer. Best treated as its own ingest path that happens to share
  our keyframe mapping, valuable independently of the AE story.

### Route D — AE-side exporter script *(recommended companion, highest fidelity)*

Ship a small ExtendScript/UXP script the user runs inside their AE. It walks the
project via Adobe's public scripting DOM and writes our project format directly,
**sampling and baking expressions** on the way out.

- **Good:** uses AE's own fully-resolved values — no guessing, no RE; it is the
  only route that can bake expressions correctly; robust across versions because
  it uses a public API; legally the cleanest (our code, Adobe's sanctioned API, no
  linking of Adobe code).
- **Bad:** requires AE installed and the user to run a script; we maintain that
  script as a small separate artifact.

### Recommendation (revised)

**Route B as the primary importer, Route D as the high-fidelity companion, Route A
as a fallback, Route C as an independent bonus ingest.**

Route B is now primary because it is the only one that works on a `.aep` a user
*received* rather than authored, which is the common case in the AE world. Route D
stays essential because it is the only route that can bake expressions, using AE's
own evaluated values through a public, sanctioned API.

The two combine into the experience we actually want: drop in a `.aep` and it
opens immediately via our own parser; if After Effects is detected on the machine
and the project uses expressions, offer to run the exporter script
(`AfterFX.exe -r script.jsx`) for a higher-fidelity pass. Good result with no
setup, great result when AE is present.

Route A survives as a fallback for AE versions whose binary layout our parser does
not yet handle — `.aepx` being text makes it the safety valve when a chunk
structure changes.

## The `.zip` archive path

> **Assumption, flagged for confirmation:** "`.zip` for `.aep` files" means
> importing a *self-contained AE archive* — a `.aep`/`.aepx` plus its footage,
> zipped the way AE's **Collect Files** output is commonly shared. If you meant
> something else (e.g. exporting our projects as a zipped AE package), the plan
> below changes and that is called out in Open Decisions.

AE's **Collect Files** produces a folder: the project file plus a `(Footage)`
directory and a report. Users zip that to share it. Handling it:

1. **Detect** a zip whose contents include a project file (`.aep`/`.aepx`) and,
   usually, a footage folder.
2. **Unpack** to a temp workspace.
3. **Resolve assets.** AE stores both absolute and relative footage paths; prefer
   the relative path against the archive root, fall back to the absolute, and on a
   miss fall through to our existing relink prompt from
   [`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md). Reject absolute paths that
   escape the archive.
4. **Ingest.** Run Route A/D on the project file, copy referenced assets into the
   target project's `assets/`, and record size + content hash so later moves can
   auto-relink.

This reuses machinery we already have. Our own shareable `.specpack` is "a zip of
project plus assets" ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md)), so the zip
plumbing, the temp-workspace handling, and the asset-hash relink logic are shared
between importing an AE archive and opening our own bundles. One implementation,
two consumers.

## The import report

A lossy import is only acceptable if it is transparent, consistent with the
"performance honesty" and "missing assets → placeholder" ethos elsewhere in the
docs. Every import ends on a report:

- **Imported** — N comps, N layers, N keyframes.
- **Approximated** — each item, with what changed (blend mode snapped, motion path
  flattened, text simplified).
- **Dropped** — each item, why, and the placeholder node that now holds its raw
  data.
- **Upgraded** — audio-amplitude expressions offered a live driver.

The report is savable alongside the project so a user can come back to it.

## Legal

- Reading a file the user authored and owns is ordinary. We ship no Adobe code,
  bundle no Adobe IP, and parse no plugin binary.
- The AE-side script uses Adobe's **public** scripting API — sanctioned
  extensibility. It is our code calling a documented interface; it does not link
  Adobe code and is GPL-compatible like the rest of the project
  ([`11-NAMING-AND-LICENSE.md`](11-NAMING-AND-LICENSE.md)).
- "After Effects" and "Adobe" are trademarks; we use them nominatively ("import
  from After Effects projects") and claim no endorsement or affiliation.
- We never redistribute Adobe's files, fonts, or effect implementations. Imported
  footage belongs to the user and travels only inside their own project.

## Testing

The `.aepx` path is genuinely testable, unlike a renderer:

- **Golden fixtures.** Author small AE projects by hand, save as `.aepx` (text, so
  it diffs cleanly and is cheap to commit), import them, and assert the resulting
  project matches an expected serialization.
- **Conversion unit tests.** Position/scale/opacity/time conversions against known
  inputs, including the Y-axis and origin convention.
- **Determinism.** Importing the same file twice is bit-identical.
- **Round-trip via Route D.** Where AE is available in a dev setup, export through
  the script and diff against the Route A result on the same project.

Keep fixtures tiny and footage-free so they do not drag in large binaries; real
media goes through Git LFS or releases per [`10-GIT-GUIDE.md`](10-GIT-GUIDE.md).

## Where this sits in the roadmap

This is **Phase 4** work ([`08-ROADMAP.md`](08-ROADMAP.md)) and should not move
earlier. It depends on things that do not exist until Phase 2 — nested comps,
text, blur/grade effects, motion paths — so importing them before they have
targets is pointless. It is also exactly the kind of large, open-ended feature the
roadmap's risk notes warn can eat the project.

Gate it behind the **research spike** below, and make an explicit go/no-go before
committing to the full importer.

## Research spike

A throwaway investigation whose only job is to answer one question: *can we parse
the core `.aepx` subset reliably enough to be worth building?* Everything here is
disposable — the spike produces a decision and a pile of notes, not shippable
code. Timebox it to roughly a week of part-time work; if it runs long, that is
itself a signal the answer is "no".

### Checklist

- [ ] **Gather fixtures.** Collect 5–10 small `.aepx` files covering the
  imports-exactly subset: a solid, a still image, a nested comp, a null with a
  parented child, transform keyframes using each of Linear / Bezier / Hold, and
  comp + layer markers. Save each from **at least two AE versions** to expose
  schema drift.
- [ ] **Map the schema.** For those fixtures, locate and document where the XML
  keeps compositions, layers, the transform property group, keyframe
  time/value/interpolation encoding, and marker data. Write down exactly which
  elements we depend on — that list is the importer's contract with AE.
- [ ] **Throwaway parser.** Build a no-UI extractor that reads the subset into our
  in-memory project model. Correctness over structure; this code is deleted after.
- [ ] **Validate conversions.** Check position normalization, the Y-axis/origin
  convention, scale %, opacity, rotation, and time against the same values read
  back inside AE. This is where "it looked right in AE" bugs are caught early.
- [ ] **Measure version fragility.** Diff the parser's output for the *same*
  project saved from two AE versions. Quantify how much breaks, since that cost is
  recurring, not one-time.
- [ ] **Prototype Route D.** Write a minimal `.jsx` that walks one comp and dumps
  its transform keyframes, and confirm that **sampling/baking an expression** to
  keyframes through the scripting DOM actually works. This de-risks the only route
  that can handle expressions.
- [ ] **Confirm the `.zip` path cheaply.** Unzip one Collect Files archive and
  resolve its footage paths against the archive root — no import, just prove the
  asset-resolution assumption in the `.zip` section holds.
- [ ] **Write the go/no-go.** One page: what imported cleanly, what fought back,
  the honest effort estimate for the real importer, and a recommendation.

### Exit criteria

Proceed to the Phase 4 importer only if **all** of these hold:

1. The imports-exactly subset parses from `.aepx` across both AE versions with no
   blocking unknowns.
2. Every value conversion round-trips within tolerance against AE's own readout.
3. Route D can bake at least one expression to keyframes.
4. The effort estimate is one both leads accept, weighed against everything else
   Phase 4 could spend that time on.

If any fail, the recorded outcome is "not now" plus the specific blocker, and the
feature stays parked rather than half-built.

## Open decisions

1. **Confirm the `.zip` interpretation.** This document assumes "import a zipped AE
   archive (Collect Files output)." Confirm, or redirect if you meant exporting
   our projects *as* a zipped AE package — which implies writing `.aepx`, a much
   larger and separate undertaking that is not planned here.
2. **Import-only, or round-trip?** This plan is one-way (AE → us). Writing `.aep`
   or `.aepx` back out is a different, much harder feature and is currently a
   non-goal. Confirm that is acceptable.
3. **Primary route.** Recommendation is `.aepx` (A) + AE-side exporter (D).
   Confirm, or raise the priority of binary `.aep` (B) if serving AE-less users
   with native files actually matters.
4. **Canvas origin/Y convention.** The position conversion needs
   [`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md) to pin down the origin (center vs
   top-left) before the importer can be correct.
