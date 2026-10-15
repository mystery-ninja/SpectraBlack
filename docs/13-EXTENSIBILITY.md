# 13 — Extensibility: Expressions, Effect Scripts, Program Extensions

This document covers the three ways Spectra Black can be extended without
recompiling the app. They are genuinely different problems with different
constraints, so they get three separate designs even though they share
infrastructure:

| Tier | What it extends | Runs | Who writes it |
|---|---|---|---|
| **Expressions** | the value of one property | on the render path, every frame | anyone, inline |
| **Effect scripts** | the set of node types | on the render path, as a node | effect authors |
| **Program extensions** | the editor itself | on UI events, off the hot path | tool/integration authors |

All three are **Phase 4** ([`08-ROADMAP.md`](08-ROADMAP.md)). None of them are
built until the systems they plug into — the property registry, the evaluator, the
frame graph, the command stack — are stable, because an extension API freezes the
shape of whatever it exposes.

## The constraints that apply to all three

These come straight from decisions already made elsewhere, and they are what keeps
an extensibility system from quietly destroying the properties the rest of the
docs fought for.

1. **Determinism is non-negotiable on the render path.** Preview/export parity
   ([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md)) means anything that influences a
   rendered pixel must be a pure function of time and scene state. Expressions and
   effect scripts therefore get no wall clock, no unseeded RNG, no I/O, and no
   frame-to-frame hidden state unless it is declared and reconstructable from a
   seek — the same rule stateful nodes already live under
   ([`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)).
2. **Sandboxed by default.** No file system, no network, no process spawning from
   render-path code. Program extensions can request those capabilities, but only
   through an explicit permission model (see Security).
3. **Bounded execution.** The render thread must never block
   ([`01-ARCHITECTURE.md`](01-ARCHITECTURE.md)). Anything on the hot path runs
   under an instruction/time budget and yields a safe value on overrun rather than
   hanging a frame.
4. **Everything edits through the command stack.** Extensions mutate the scene via
   the same `Command` objects the UI uses ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md)),
   so undo, redo, and the single-writer rule hold with no special cases.
5. **It compiles to what we already have.** Expressions and scripted effects lower
   into the flat `EvalPlan` ops and the frame graph, so the render path does not
   know or care that a value came from an expression rather than a keyframe
   ([`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md)).

## Tier 1 — Expressions

### What they are

A small formula attached to a property that computes its value from other things
in the project: time, the playhead, audio signals, and other properties. This is
the feature [`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md) deliberately defers to
Phase 4; this section is its full design, and that deferral still stands.

An expression is a fourth source for a property, alongside the static value, the
keyframe track, and the drivers. It can produce the base value or feed a driver
slot, and it lowers to an `EvalPlan` op like everything else.

### The language

AE uses a full JavaScript engine, which is exactly how AE projects become slow and
unmaintainable. We do not repeat that. Per the requirement already written into
[`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md) — sandboxed, deterministic, no
file or network access, fast enough for thousands of evaluations per frame — the
recommendation is:

**A small purpose-built expression DSL, not a general embedded language.**

- No garbage collector on the hot path, which a per-frame Lua call cannot promise.
- Total control over the sandbox: the language simply has no syntax for I/O or
  time-of-day, so there is nothing to lock down after the fact.
- It compiles to bytecode / an `EvalPlan` op at edit time, not parsed per frame.
- A familiar C-like, AE-flavoured syntax so the audience that wants expressions
  recognizes it immediately.

Lua is the fallback if the DSL proves too limiting, but it belongs more naturally
in Tier 3, where the hot-path and GC constraints do not apply.

### What an expression can see

The access surface is deliberately close to the driver vocabulary
([`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)) plus cross-property references:

- `t`, `trackPos`, composition and layer local time
- audio signals: `rms`, `bass`/`mid`/`high`, `band(n)`, `beat`, `beatPulse`,
  `beatPhase`, `peakBand`
- other properties by path, resolved at the same `t`
- math, easing, `clamp`, `remap`, and seeded `noise(seed)` / `random(seed)`

That is the whole surface. No globals, no app object, no document access — a
property expression computes a number, nothing more.

### Cross-property references and cycles

References across properties form a dependency graph. When the scene topology
changes, the evaluator topologically sorts the ops so a referenced property is
computed before the one that reads it, which is a natural extension of the flatten
step already described in [`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md). A cycle
is a compile-time error surfaced in the UI, not a runtime hang.

### Relationship to imported AE expressions

[`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md) says AE's JavaScript
expressions are **baked to keyframes on import, never executed**, and that remains
true — we cannot and will not run AE's JS. Our native expression engine is a
separate thing that runs our own deterministic DSL. A later, speculative nicety is
to *translate* trivial AE expressions (`time*10`, a basic `wiggle`) into our DSL
on import instead of baking them, but that is a low-priority enhancement, not a
dependency.

## Tier 2 — Effect scripts

### What they are

A way to author new **node types** — effects and generators — and ship them as
content, without recompiling the app or writing native code. This is the
"Plugin API for custom nodes and shaders" and "Shader node with live GLSL editing"
roadmap items ([`08-ROADMAP.md`](08-ROADMAP.md)), generalized into a packageable
format.

### Primary form: shader + manifest

The clean path, because it fits the pipeline we already have:

- A **GLSL fragment shader** (or a short pass chain) written against the same
  cross-platform shim as built-in renderers ([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md)),
  so it runs on desktop GL and Android GLES unchanged.
- A **declarative manifest** of the node's properties, in the same shape the
  built-in `describe()` registry produces ([`01-ARCHITECTURE.md`](01-ARCHITECTURE.md)).
  From that one declaration the node gets its inspector UI, keyframe support,
  driver support, serialization, and a stable save-format name — for free, exactly
  like a built-in node.
- A declaration of the node's **frame-graph inputs and outputs**, so the pass
  planner can slot it in, alias its render targets, and order it correctly.

A shader effect is deterministic by construction: it is a pure function of its
inputs, the current time, and its source textures. That is why it is the preferred
form — it cannot violate the render-path rules even if the author is careless.

### Secondary form: sandboxed CPU logic

Some effects need per-frame CPU work that a fragment shader cannot express cleanly
(custom particle behaviour, procedural layout). Those use the same sandbox and the
same determinism rules as expressions: bounded execution, no I/O, declared state
only. The script produces parameters or geometry that a built-in renderer draws;
it does not get raw GL access.

### What effect scripts are *not*

**Not native compiled node plugins.** A `.so`/`.dll` that links the renderer and
runs arbitrary C++ is the same category of problem as AEX hosting
([`00-VISION.md`](00-VISION.md)): platform-specific, ABI-fragile, unsafe to run
from a shared file, and hostile to the Android target. We deliberately choose
shader + manifest + sandboxed script over native effect binaries. If a native
escape hatch ever exists, it lives in Tier 3 under its warnings, not here.

### Packaging

An effect pack is a manifest plus its shaders and optional script, zipped and
versioned — reusing the same zip plumbing as `.specpack` and the AE-archive
importer ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md),
[`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)). An unknown effect pack
referenced by a project degrades to the placeholder-node behaviour already
specified for unknown node types, so a scene never fails to open because a pack is
missing.

## Tier 3 — Program extensions

### What they are

Extensions to the **editor**, not the visual output: new panels, importers and
exporters, batch tools, menu commands, and integrations. Explicitly the thing you
called out as "extensions / program plugins (not effects)". The AE importer in
[`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md) is the archetype — and a good
candidate to eventually be rebuilt *on* this API rather than baked in.

### Why this is a different runtime

Editor extensions run on UI events — a menu click, a file open — not inside the
16.6 ms frame budget. The hot-path and no-GC constraints that forced a tiny DSL in
Tier 1 do not apply here, so a richer embedded language is acceptable. The
recommendation is **Lua** for the scripted extension layer: small, embeddable,
GPL-compatible, and a well-trodden choice for exactly this.

### The editor API

A stable, versioned surface that lets an extension:

- register commands, menu items, and keyboard shortcuts
- add a panel (rendered through the host, not with raw GL access)
- register an importer or exporter for a new file type
- read and modify the scene — **only through the command stack**, so every
  extension action is undoable and respects the single-writer rule
- subscribe to editor events (project opened, selection changed, export finished)

Extensions never touch the render thread, the audio thread, or the live scene
snapshot directly. They act on the document through commands and let the normal
pipeline pick the changes up on the next frame
([`07-UI-UX.md`](07-UI-UX.md) already relies on this for live editing).

### The native escape hatch (discouraged)

A compiled native extension (`.so`/`.dll`) is the only way to do genuinely heavy
or system-level integration, and it is the sharp tool: desktop-only, platform-
specific, unsafe, and ABI-fragile. It is documented as an advanced, opt-in,
desktop-only option with loud warnings, and it is never the recommended path.
Everything most extensions want is reachable from the scripted API.

## Security and distribution

This is the section that is cheap to get right now and very expensive to retrofit,
so it is decided here rather than discovered later.

Scenes, bundles, and packs are **shared** — that is the whole point of a preset
community ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md)). A file that silently
runs code when opened is a malware vector. The policy:

- **Expressions and shader/CPU effect scripts are sandboxed and safe to run
  automatically.** They have no capability to do harm — no I/O, bounded execution
  — so opening a shared scene that contains them is safe by construction.
- **Program extensions are never auto-run from a shared scene or bundle.** They are
  installed deliberately, and any capability beyond the pure editor API (file
  access, network, native code) is **off by default and granted per extension with
  explicit user consent.** A downloaded preset cannot smuggle in a tool extension.
- Packs record a content hash and version, so tampering is detectable and a project
  can pin the pack version it was authored against.

## Licensing

Consistent with [`11-NAMING-AND-LICENSE.md`](11-NAMING-AND-LICENSE.md):

- The extension API is GPL-3.0 like the rest of the project. A **native** plugin
  that links the app is a derivative work and inherits that obligation.
- Expressions, shaders, and scripts an author writes are their own content under
  their own terms; we neither claim them nor sandbox their *licensing*, only their
  *capabilities*.
- The embedded language and any bundled interpreter must be GPL-compatible — Lua
  (MIT) and a hand-rolled DSL both are.

## Reconciling with the non-goals

- **"No AEX, ever"** ([`00-VISION.md`](00-VISION.md)) is untouched: we host no
  Adobe plugin binaries, and our own native escape hatch is our ABI, opt-in, and
  discouraged — not a compatibility layer for anyone else's.
- **"No expression language in Phase 1–2"** ([`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md))
  is untouched: expressions remain Phase 4. This document designs them; it does not
  pull them forward.

## Roadmap and sequencing

All three are Phase 4, and they have a natural order within it:

1. **Expressions** first — smallest surface, highest everyday value, and it
   exercises the deterministic-sandbox machinery the other two reuse.
2. **Effect scripts** second — depends on the shader node and a stable property
   registry and frame graph already existing.
3. **Program extensions** last, and only if demand appears after the first two —
   the largest API surface and the one whose mistakes are hardest to walk back, so
   it benefits from the sandbox and permission model being proven first, and is
   not built on speculation.

Each should be gated the way the AE importer is
([`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)): a small spike that proves
the determinism and sandbox story on a real example before the full API is frozen.

## Testing

- **Determinism.** The headline test for Tiers 1 and 2: evaluate an expression or
  scripted effect twice at the same `t` and require bit-identical output, and run
  the preview/export parity test ([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md))
  with expression- and script-driven properties in the scene.
- **Sandbox escape.** Assert that expression/effect code genuinely cannot reach the
  file system, network, clock, or unseeded randomness — negative tests, run in CI.
- **Bounds.** A deliberately runaway expression must be cut off and yield a safe
  value within budget, never hang a frame.
- **Cycle detection.** Cyclic property references fail at compile with a clear
  error, not at runtime.
- **Permission model.** A shared scene carrying a capability request must not
  acquire that capability without explicit consent — tested, because it is a
  security property, not a convenience.

## Settled for Tier 3

**Design now, build later — only if demand appears.** Program extensions stay in
this document as an API sketch and a sequencing target, but they are not committed
work for the first Phase 4 pass. Expressions and effect scripts ship first; Tier 3
is unlocked only when real use cases show up that Tiers 1 and 2 cannot cover
(importers, panels, batch tools, integrations). That keeps the largest and hardest-
to-walk-back surface from being built on speculation.

## Open decisions

1. **Expression language: DSL or Lua?** Recommendation is a purpose-built DSL on
   the hot path and Lua reserved for Tier 3. Confirm, or accept Lua everywhere for
   the simplicity of one runtime at the cost of GC on the render path.
2. **Scope of the first effect-script release.** Shader-only to start, with
   sandboxed CPU logic deferred? Recommendation: yes, shader + manifest first.
3. **Native plugin escape hatch: offer it or refuse it?** Refusing keeps the
   safety and portability story clean; offering it serves power users at real cost.
   Leaning refuse until there is a concrete need the scripted API cannot meet.
