# 05 — Animation Model

This is the heart of the project. Everything else is a well-understood
engineering problem; this is the part nobody has built properly.

> Depends on confirming **Option C** from [`00-VISION.md`](00-VISION.md). This
> document assumes it.

## The property

Every animatable value in the scene is a `Property<T>` with three parts:

```cpp
template <typename T>
struct Property {
    T                   staticValue;   // used when no keyframes exist
    KeyframeTrack<T>    keyframes;     // optional; empty is the common case
    std::vector<Driver> drivers;       // optional; audio-reactive modifiers
};
```

Evaluation:

```
1. base = keyframes.empty() ? staticValue : keyframes.evaluate(t)
2. for each driver d:
       signal = d.signal.sample(analysisCache, t)
       shaped = d.shaping.apply(signal)
       base   = d.blend(base, shaped * d.amount)
3. return base
```

Three behaviors fall out of one mechanism:

| Keyframes | Drivers | Behaves like |
|---|---|---|
| none | none | A constant |
| yes | none | **After Effects** |
| none | yes | **Avee** |
| yes | yes | **The reason this app exists** |

That last row is worth sitting with. Consider a logo's scale:

- Keyframes animate it from `0.0` to `1.0` over the first two seconds — a
  designed, precisely-timed entrance.
- A `beatPulse` driver with `multiply` blend and amount `0.15` adds a punch on
  every kick.

The logo flies in on a schedule *and* breathes with the music. In After Effects
that requires an expression referencing a baked audio layer, which breaks the
moment you change the track. In Avee it is simply impossible. Here it is two
clicks.

## Keyframes

Conventional, because this is solved and users have correct expectations.

```cpp
template <typename T>
struct Keyframe {
    double       time;
    T            value;
    Interpolation in;      // how we arrive
    Interpolation out;     // how we leave
    Vec2 handleIn, handleOut;  // bezier control points
};

enum class Interpolation { Hold, Linear, Bezier, Ease, EaseIn, EaseOut };
```

Notes on the details that matter:

- **Bezier handles are the default** for new keyframes, with auto-smoothed
  tangents. Linear-by-default produces robotic motion and users who do not know
  to change it will blame the tool.
- **Separate in/out interpolation** per keyframe, so you can ease into a hold.
- **Hold** is essential for anything discrete — visibility, text content, indices.
- Keyframe times are `double` seconds, not frame integers. Project frame rate
  must be changeable after the fact without destroying timing.

### Spatial interpolation

For 2D position specifically, keyframes additionally interpolate along a
**motion path** with its own bezier curve in space, independent of the temporal
curve. This is a thing After Effects gets right and most imitators miss: the
curve through space and the curve through time are genuinely different concerns.
Deferred to Phase 2, but the data model must leave room for it now, because
retrofitting it means a format migration.

## Drivers

```cpp
struct Driver {
    SignalSource  signal;     // which audio signal
    ShapingChain  shaping;    // attack/decay, delay, curve, random
    float         amount;     // scale
    BlendOp       blend;      // Add, Multiply, Replace, Max, Min
    bool          enabled;
};
```

### Blend operators

The operator choice is more important than it looks, because it determines
whether a driver *decorates* the keyframe animation or *overrides* it:

| Op | Result | Typical use |
|---|---|---|
| `Add` | `base + shaped * amount` | Shake, offsets, jitter |
| `Multiply` | `base * (1 + shaped * amount)` | Pulse, scale punch — **respects base animation** |
| `Replace` | `lerp(base, shaped, amount)` | Pure Avee behavior, ignore keyframes |
| `Max` / `Min` | `max(base, shaped*amount)` | Floors and ceilings |

`Multiply` is the one that makes hybrid animation feel right. If the logo scales
from 0 to 1 and a beat driver *adds* 0.15, the logo is visible at the start when
it should be invisible. If it *multiplies* by `1 + 0.15·beat`, the pulse scales
with the entrance, and at `t=0` the logo is still correctly at zero. Default new
drivers to `Multiply` for scale-like properties and `Add` for position-like ones.

### Shaping chain

Ordered operations between raw signal and final contribution:

```
raw signal  →  [remap]  →  [curve]  →  [attack/decay]  →  [delay]  →  [random]  →  out
```

- **Remap** — input range to output range, with invert
- **Curve** — ease, exponential, or a custom curve editor. An exponential curve
  on bass energy is the difference between "mushy" and "punchy"
- **Attack/decay** — additional envelope on top of the analysis-level smoothing,
  per-driver
- **Delay** — offset in seconds or in beats. Beat-relative delay is how you build
  cascading, staggered reactions across many layers
- **Random** — new random value per beat, with a seed. This is how Avee's
  `BeatRandomShake` works, generalized

Every stage must be deterministic. The random stage is seeded from
`(driverId, beatIndex)` rather than a running RNG, so scrubbing backward gives
the same values. This is a small detail with a large correctness payoff.

## The UI for this

The data model is only half the problem. Avee's property UI is a flat list of
sliders; After Effects' is a timeline with a twirl-down hierarchy. We need to
show *three* things per property — static value, keyframe track, and drivers —
without it becoming a mess.

Proposal, to be prototyped before committing:

```
▼ Logo
  ▼ Transform
      Position     [ 0.500 ] [ 0.400 ]   ◆──────◆────  [+ driver]
        ↳ ⚡ beat → shake                  amount 0.04   [×]
      Scale        [ 1.000 ]              ──◆────────   [+ driver]
        ↳ ⚡ beatPulse × multiply          amount 0.15   [×]
      Rotation     [ 0.000 ]                            [+ driver]
```

The property row shows the current value. A stopwatch toggle enables keyframes
and reveals the track. Drivers appear as indented child rows with a lightning
glyph, each with inline amount and a disable toggle. Critically, a property with
no keyframes and no drivers shows **just a number** — the complexity is strictly
opt-in and invisible until requested.

See [`07-UI-UX.md`](07-UI-UX.md).

## Expressions — deliberately deferred

After Effects has a full JavaScript expression engine. It is immensely powerful
and it is also where AE projects go to become unmaintainable and slow.

**Phase 1 and 2 ship no expression language.** The driver system covers the large
majority of what expressions get used for in music videos, with a UI that is
discoverable instead of requiring you to know an API.

If we add one in Phase 4, the requirements are: sandboxed, deterministic, no
file or network access, and fast enough to evaluate thousands of times per frame.
Lua or a small purpose-built expression language, not JavaScript. And it must
compile to the same evaluated-property representation as everything else so the
render path does not need to know the difference.

The full design — expressions as a property source, the language choice,
cross-property dependency graphs, and how they relate to the scriptable effects
and editor extensions — is in [`13-EXTENSIBILITY.md`](13-EXTENSIBILITY.md).

## Evaluation performance

The evaluator runs on every property of every node every frame, so its structure
matters more than its arithmetic.

**Compile the scene to a flat evaluation plan.** Walking a pointer-chasing node
tree each frame and virtual-dispatching per property is how you turn 0.3 ms into
4 ms. Instead, when the scene topology changes, flatten it:

```cpp
struct EvalPlan {
    std::vector<KeyframeOp> keyframeOps;  // contiguous, cache-friendly
    std::vector<DriverOp>   driverOps;
    std::vector<float>      output;       // evaluated values, indexed by slot
};
```

Each frame walks three flat arrays with no branching on node type and no pointer
chasing. Renderers then read from `output` by slot index. The plan is rebuilt
only when the scene *structure* changes, which is to say on edits, not on
playback.

This also makes the evaluator trivially parallelizable later if a scene ever gets
heavy enough to need it, since the ops are independent by construction.

## What this unlocks

Features that fall out of this model essentially for free, which is the best
evidence the model is right:

- **Snap keyframes to the beat grid** — the grid exists, keyframes are time-based
- **Stagger a reaction across layers** — beat-relative delay, driven by layer index
- **Retime to a new track** — keyframes stay, drivers re-derive from the new
  analysis. Swapping the song does not destroy the animation, which is impossible
  in AE
- **Reusable scene presets** — a scene with only drivers and no keyframes is
  track-agnostic, exactly like an Avee preset. Avee's entire sharing ecosystem
  works this way and we should preserve it
- **Bake drivers to keyframes** — for when someone needs to hand-tune a reaction.
  One menu command, since both are the same property
