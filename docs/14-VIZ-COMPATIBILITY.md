# 14 — Avee Project Import (`.viz`)

## Why this document exists, and why it comes first

[`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md) plans the After Effects
import in depth. The brainstorming asks for **both** `.aep` and `.viz` support,
but only the AE half was written down. That gap matters, because on every axis
that should decide priority, the Avee import wins:

| | `.aep` (After Effects) | `.viz` (Avee) |
|---|---|---|
| Format | Undocumented binary RIFX, or version-fragile XML | **Plain JSON inside a zip** |
| Do we understand it? | No — needs a reverse-engineering spike | **Yes — we have readable decompiled source** |
| Needs the source app installed? | Yes, for the high-fidelity route | **No** |
| Audience size | Small AE visualizer community | **Large Avee community** |
| Is it our beachhead? | No | **Yes** ([`00-VISION.md`](00-VISION.md)) |
| Concept mapping | Lossy; AE has features we lack | **Near-complete; we are a superset** |

[`00-VISION.md`](00-VISION.md) names the Avee power user as the beachhead
audience: *"They are underserved, they are already in communities we can reach,
and they will tolerate rough edges in exchange for the ceiling being lifted."*
The single most effective way to reach them is for their existing visualizers to
open in our app.

The strategic point is sharper than mere ordering. **The AE import is the hard
path to the smaller audience; the Avee import is the easy path to the larger
one.** It should land first, and it is cheap enough to be a Phase 2 feature
rather than Phase 4.

There is also a direction asymmetry worth noticing. Importing from AE is lossy
because AE has concepts we deliberately lack (3D, its effect catalog, masks).
Importing from Avee is close to lossless because **we are a superset of Avee by
design** — everything Avee can express, we can express, plus a timeline. An Avee
user loses nothing by switching, which is exactly the migration story we want.

## The format

Verified by reading the decompiled source, not guessed.

A `.viz` file is a **zip archive** containing a serialized scene plus its
referenced assets (`Design/VisualizerSharingDesign.java` packs
`serializePretty()` output alongside the visualizer folder's files). `.zip` is
accepted interchangeably.

The scene itself is **plain JSON**, via `org.json`:

```
CustomScene      → JSONObject
                   ├── "ver"           version id string
                   └── "compositions"  JSONArray
CustomComposition → JSONObject
                   └── "elements"      JSONArray
CustomPropertiesList → JSONObject
                   ├── "_name"         customization/type name
                   └── <property keys>  floats, ints, strings, nested objects
```

Properties are written by the same `onReadCustomization` calls catalogued in
[`09-AVEE-TEARDOWN.md`](09-AVEE-TEARDOWN.md) — `putPropertyFloatAsRange`,
`putSelectString`, `putPropertyIntAsCRGBA`, `putEmptyChild` — so the key names in
the JSON are exactly the property names in the source. **The decompiled source is
effectively the format specification**, which is a luxury the AE importer does not
have.

Two decoding details:

- **Colors** are packed signed 32-bit ARGB ints (`putPropertyIntAsCRGBA`). Note
  `-553648128` for the default fade color — unpack as unsigned ARGB, not RGBA.
- **`MeasuredVar` is a space-separated string**, `"<measure> <argA> <argB>"`, per
  `MeasuredVar.toString()` / `FromString()`. This is the single most important
  field to parse correctly, because it carries all of Avee's audio reactivity.

## Element mapping

Avee's registry (`ElementsFactory`) has fifteen types. Nearly all have a direct
target:

| Avee type | Spectra Black | Notes |
|---|---|---|
| `Root` | `Project` root | Direct |
| `Composition` | `Composition` | Direct; nesting included |
| `Bars` | `SpectrumBars` | Direct — Phase 1 node |
| `Image` | `Image` | Direct; asset from the archive |
| `Text` | `Text` | Metadata tokens map, below |
| `Particles` | `Particles` | Phase 2 on our side |
| `AudioProvider` | audio source binding | Becomes an explicit reference, below |
| `BlurEffect` | `Blur` | Phase 2 |
| `RgbSplitEffect` | `RgbSplit` | Phase 2 |
| `MotionBlurEffect` | `MotionBlur` | Phase 2 |
| `MirrorEffect` | `Mirror` | Phase 2 |
| `ColorCorrection` | `ColorGrade` | Phase 2 |
| `DummyElement` | shader node | Maps to our `.sbx` shader node ([`13-EXTENSIBILITY.md`](13-EXTENSIBILITY.md)) |
| `StatText` | diagnostic text | Low value; import as plain text |
| `AppLogo` | **dropped** | Avee's own branding watermark. Not ours to reproduce |

Because effects are tree nodes in both systems
([`09-AVEE-TEARDOWN.md`](09-AVEE-TEARDOWN.md)), the tree structure transfers
directly. No restructuring, unlike AE's separate per-layer effect stacks.

Avee's text metadata tokens (`{Artist}`, `{Title}`, `{TimeCurrent}`,
`{ArtistAndTitle}`, …) map onto our own metadata fields one for one.

### The RE-mod effects

The brainstorming's effect list includes items from the Avee RE mod — twirl,
liquify, kaleidoscope, FOV, zoom blur, pixels, 3D box, edges, godrays, spherify,
curve, bulge/pinch. These are **not** in stock Avee 1.2.101, so a `.viz` from a
modded build may contain element types the stock registry never had. Treat
unknown types with the placeholder-node rule from
[`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md) so a modded scene still opens, and
add mappings as we implement each effect.

## `MeasuredVar` → `Driver`: the important part

This is where the import becomes an *upgrade* rather than a transfer, and it is
the most valuable mapping in either importer.

[`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md) already identifies the flaw:
Avee bakes the shaping into the measure *name*, which is why `BeatCamShakeMore`
and `BeatCamShakeLess` exist as separate measures differing only in magnitude.
Our drivers parameterize that. So the import decomposes each Avee measure into
signal plus shaping:

| Avee measure | Signal | Shaping | Blend |
|---|---|---|---|
| `Constant` | — | static value | — |
| `Nothing` | — | no driver | — |
| `Beat` | `beat` | — | Add |
| `BeatCamShakeMore` | `beat` | random per beat, large range | Add |
| `BeatCamShakeLess` | `beat` | random per beat, small range | Add |
| `BeatRandomShake` | `beat` | random per beat, seeded | Add |
| `ConstantShakeMore` / `ConstantShake` | `noise` | continuous, large / small range | Add |
| `BeatCamShakeRotMore` / `…RotLess` | `beat` | random per beat → rotation | Add |
| `BeatTriggerAnim` | `beatPulse` | decaying spike | Multiply |
| `PeakBarIndex` | `peakBand` | — | Replace |
| `TrackPosition` | `trackPos` | — | Replace |
| `TotalTime` / `TotalTimeBackward` | `t` | linear, optionally inverted | Replace |
| `TotalTimeWhenPlaying` | `t` | gated on playback | Replace |
| `TotalTimeAndBeat` | `t` + `beat` | two drivers on one property | Add |
| `IsPlaying` | playback state | boolean | Replace |

`measureArg.x` and `.y` become the shaping range. The `More`/`Less` pairs collapse
to one signal with different amounts — so after import the user can set a value
Avee's authors never offered, which is the first thing they should notice.

### The upgrade moment

An imported Avee scene is, by construction, a scene with **drivers and no
keyframes** — which [`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md) notes behaves
exactly like Avee. It will look and behave the way it did, and the timeline will
simply be empty and waiting.

That is the ideal onboarding: their work opens, looks right, and the new
capability is visible but not forced. Worth making the post-import message say
precisely that.

### `AudioProvider` and implicit binding

The brainstorming proposes that *"the app takes the last AudioProvider in the
layers as reaction factor."* That matches Avee, but it is worth flagging as a
design smell rather than copying.

Implicit "last one wins" ordering means **reordering layers silently changes which
audio drives the scene**, with no visible cause. That is a reliable source of
"why did my visualizer break" reports, and it is the kind of hidden state the
architecture otherwise avoids.

Recommended instead: import each `AudioProvider` as a named **audio source**, and
have nodes reference it explicitly — defaulting to the single source when there is
only one, which is the overwhelmingly common case. Identical behavior for simple
scenes, no hidden coupling, and multiple audio sources become possible rather than
ambiguous. The importer resolves Avee's implicit rule once, at import, and writes
explicit references.

## Fidelity: do we reproduce Avee's bugs?

The genuinely interesting question, and it needs a decision rather than a default.

[`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md) documents that Avee's spectrum lacks
spectral tilt compensation, so bass dominates and reverb tails pin the low bars —
the bug called out in the brainstorming. Our analysis fixes that.

Which means **a correctly imported `.viz` will not look like it did in Avee.** The
bars will be more even and more responsive. By our standards that is better; to a
user checking whether their scene survived, it is "the import is broken."

Options:

1. **Import with an `Avee-compatible` analysis preset** — flat tilt, Avee's
   smoothing constants, matching band mapping — so it looks identical on open, and
   offer a one-click "modernize audio response" afterward.
2. Import with our corrected defaults and explain the difference in the report.
3. Ask at import time.

**Recommendation: option 1.** The scene should open looking the way its author
made it, with the improvement offered rather than imposed. It also gives us a
concrete, testable definition of success — pixel comparison against Avee's own
output on the same scene and track — which option 2 cannot provide.

This argues for the tilt control in [`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)
being a first-class, serialized per-project setting rather than a global
preference.

## Import report

Same contract as the AE importer: every import ends with what imported, what was
approximated, what was dropped and why, and placeholder nodes holding the raw data
for anything unmapped. See [`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md).

## Where this sits in the roadmap

**Phase 2**, not Phase 4.

It is gated only on the nodes it targets existing — bars, image, and text land in
Phase 1, and the effects in Phase 2. There is no reverse-engineering spike needed,
because the format is JSON and we hold the source that writes it. The work is
genuinely a parser plus a mapping table.

Suggested sequencing within Phase 2: import the Phase 1 subset first
(composition, bars, image, text, transforms, measures), which already covers a
large share of real Avee scenes, then extend as each effect node lands.

## Testing

Easier to test than anything else in the project, because both ends are JSON:

- **Fixture scenes.** Commit small `.viz` files covering each element type and a
  representative spread of measures. They are tiny and text-based, so they diff
  cleanly — strip bundled assets to keep binaries out of git, per
  [`GIT-CHEATSHEET.md`](GIT-CHEATSHEET.md).
- **Mapping unit tests.** Every `MeasureDefs` entry maps to the expected
  signal/shaping/blend triple. This table is the importer's core and should be
  exhaustively covered.
- **Parse robustness.** Truncated, reordered, and unknown-key JSON must degrade to
  placeholders rather than failing to open.
- **Visual parity.** With `Avee-compatible` analysis, render an imported scene and
  compare against Avee's own output for the same scene and track. The strongest
  possible evidence the importer is correct.
- **Color round-trip.** Signed ARGB ints decode correctly, including the negative
  values Avee's defaults use.

Real `.viz` projects to test against — several of them shipped — are on disk
already; see the reference-material table in
[`CONTRIBUTING.md`](../CONTRIBUTING.md). Prefer those over inventing fixtures,
since the point is to survive what Avee actually writes.

## Open decisions

1. **Confirm the priority inversion.** This document argues `.viz` import belongs
   in Phase 2 and ahead of `.aep` import, reversing the implied order. Confirm.
2. **Confirm `Avee-compatible` analysis on import** (option 1 above) rather than
   silently applying our corrected audio response.
3. **`AudioProvider`: implicit last-wins or explicit references?** Recommendation
   is explicit, resolved at import.
4. **Modded `.viz` files.** Do we commit to mapping the Avee RE mod's extra
   effects, or only stock 1.2.101 plus placeholders? Affects how much of the
   community's existing work opens cleanly.
5. **Export back to `.viz`?** Currently out of scope and one-way, matching the AE
   decision. An Avee user who migrates cannot go back — confirm that is acceptable.
