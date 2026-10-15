# 07 — UI and UX

## Design principle

**Open the app, drop in a track, see something moving immediately.**

This is the Avee posture from [`00-VISION.md`](00-VISION.md) and it is the single
most important UX decision in the project. After Effects presents you with an
empty gray void and a demand that you know what a composition is. Avee presents
you with something already pulsing to your music, and you discover the depth by
poking at it.

We want Avee's on-ramp with After Effects' ceiling. In practice that means a new
project is not empty — it opens with a usable default scene (spectrum bars, a
background, track title text) that already reacts. Users learn by modifying
something that works rather than by assembling something from nothing.

## Layout

Standard four-panel compositor layout, because fighting established conventions
costs users and buys nothing:

```
┌──────────────────────────────────────────────────────────────────┐
│  menu · transport · render                                       │
├────────────┬────────────────────────────────────┬───────────────┤
│            │                                    │               │
│  Scene     │           Viewport                 │  Inspector    │
│  tree      │        (live preview)              │  (properties  │
│            │                                    │   of the      │
│  + add     │                                    │   selection)  │
│            │                                    │               │
├────────────┴────────────────────────────────────┴───────────────┤
│  Timeline  ·  waveform  ·  beat grid  ·  keyframe tracks         │
└──────────────────────────────────────────────────────────────────┘
```

Dear ImGui's docking branch means users can rearrange and detach this, which
matters on a multi-monitor setup. Ship sensible presets: "Design", "Animate"
(tall timeline), "Preview" (viewport only).

## The timeline

The panel where the two parent apps actually have to reconcile, so it needs the
most thought.

```
      0:00      0:15      0:30      0:45      1:00      1:15
      │         │         │         │         │         │
 ░▒▓█▓▒░▒▓███▓▒░▒▓█▓▒░▓███████▓▒░▒▓█▓▒░▒▓███████▓▒░▒▓█▓▒░   ← waveform
 ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎ ╎   ← beat grid
                    ▼ intro        ▼ drop                   ← markers
 ──────────────────────────────────────────────────────────
 ▼ Logo
     Position    ◆─────────◆                                ← keyframes
       ⚡ beat    ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬         ← driver span
     Scale                 ◆────────────◆
 ▼ Bars
     Height      (no keyframes — driver only)
       ⚡ band    ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬
```

Key decisions:

- **The waveform and beat grid are always visible**, at the top, above the
  layers. In AE the audio is a layer like any other and is usually collapsed. Here
  the music is the organizing substrate of the whole project and should look like
  it.
- **Snapping to the beat grid is on by default.** Dragging a keyframe snaps to
  beats, then to subdivisions when zoomed in. Hold a modifier to snap freely.
  This is the feature that makes precisely-timed-to-music work pleasant instead of
  tedious, and it only exists because we estimate tempo offline.
- **Drivers render as spans, not curves,** by default — a driver is active over a
  range rather than at points. Expanding one shows the resolved curve, which is
  useful for debugging why something is not reacting as expected.
- **Markers** for song sections, auto-populated from section detection in Phase 3
  and manually placed before then.

### Zoom

Zoom to fit, zoom to selection, and zoom around the playhead. Horizontal scroll
with shift-wheel. Standard, but worth stating because getting timeline navigation
wrong makes a tool feel bad in a way users struggle to articulate.

## The viewport

- Live preview at panel resolution, not project resolution, for speed.
- A toggle to preview at full project resolution for checking detail.
- On-canvas transform handles: move, scale, rotate, anchor point.
- Safe-area and rule-of-thirds overlays.
- **A resolution indicator**, always visible. Users need to know when they are
  looking at a 480p proxy of a 4K project.

### Performance honesty

Show actual frame time, and when the preview cannot keep up, say so explicitly
rather than silently dropping frames. A small "preview: 42fps (project 60fps)"
readout prevents the user from concluding the export will stutter too.

## Masking: take Avee's model, not AE's

From the brainstorming:

> *"I like the idea for masking in Avee Player more than in AE cuz in avee you
> select an image or composition and on the 'Mask Image' you can simply pick a
> shape, either from another composition or something else."*

The feature being described is what After Effects calls a **track matte** — using
one layer's alpha or luminance to mask another. The instinct that Avee's version
is better is correct, and the reason is worth writing down because it generalizes.

**AE binds the matte by layer order.** The matte must be the layer *directly
above* its target, and selecting a track matte silently switches that layer off.
Nothing on screen explains the relationship, moving an unrelated layer can break
it, and it is a well-known stumbling block for new users.

**Avee binds it by explicit reference.** You pick the mask source from a dropdown.
The relationship is visible, it survives reordering, and one source can mask
several targets.

This is the same principle as the `AudioProvider` discussion in
[`14-VIZ-COMPATIBILITY.md`](14-VIZ-COMPATIBILITY.md): **explicit references beat
implicit ordering.** Order-dependent behavior is invisible state, and invisible
state is where users lose hours.

So masking is a property on the masked node — a reference to another node or
composition, plus a mode (`Alpha`, `Inverted Alpha`, `Luma`, `Inverted Luma`). It
shows in the inspector like any other property, and the frame graph
([`03-RENDER-ENGINE.md`](03-RENDER-ENGINE.md)) already handles one node consuming
another's output, so it needs no new rendering concept.

One consequence to accept deliberately: an AE user will look for track mattes in
the layer stack and not find them. The AE importer
([`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)) can translate AE's
order-based mattes into our explicit references on import, which is strictly
easier than the reverse would be.

## The inspector

Generated entirely from the property registry described in
[`01-ARCHITECTURE.md`](01-ARCHITECTURE.md), which means a new node type gets a
complete UI for free.

```
Logo                                    [Image]
─────────────────────────────────────────────────
▼ Transform
    Position      0.500   0.400      ⏱  ⚡
    Scale         1.000              ⏱  ⚡
    Rotation      0.0°               ⏱  ⚡
    Anchor        Center  ▾
▼ Appearance
    Opacity       100%               ⏱  ⚡
    Blend Mode    Alpha   ▾
▼ Source
    Image         logo.png           [browse]
```

Two glyphs per animatable property: a stopwatch (enable keyframes, as in AE) and
a lightning bolt (add a driver). Both are quiet until used. The visual weight of
a property that is static, keyframed, or driven should be immediately
distinguishable — color-code the value field.

### Sliders

Avee's community specifically asked for higher slider sensitivity, and the RE
project added it, which tells us the default was wrong in a way that annoyed
people for years. Lessons taken:

- Drag for coarse, modifier-drag for fine, double-click to type an exact value.
- Right-click for reset to default, add keyframe, add driver.
- Values are never clamped to the slider's display range when typed. The slider
  shows a *useful* range, not the *legal* range.

## Interaction details that matter

Small things that separate tools that feel good from tools that feel cheap:

- **Spacebar plays and pauses.** Always, from any panel focus.
- **Scrubbing is audible.** Grain playback while dragging the playhead.
- **Edits apply live during playback** without stopping it. This is a major part
  of why Avee feels fun and AE feels like work, and it falls out of our
  architecture: the scene snapshot per frame means an edit is picked up on the
  next frame automatically.
- **No modal progress dialogs** except for export. Analysis runs in the
  background with a subtle indicator.
- **Undo covers everything**, including property edits made during playback.

## Android UI

A genuinely different design, not a reflow of the desktop one. Compose-based,
touch-first, with roughly this scope for Phase 3:

- Browse and play tracks, pick a scene preset, full-screen reactive preview.
- Edit property values and driver amounts via touch-friendly controls.
- Export to video.
- **No keyframe editing on phone** initially. Fine-grained timeline manipulation
  on a 6-inch touchscreen is a bad experience and a large amount of work; tablet
  support can revisit it.

This effectively makes Android "Avee, but with our engine and our export," which
is a genuinely compelling product on its own and a natural distribution channel
to the exact users we most want.

## Accessibility and polish

Not Phase 1, but decide early because retrofitting is expensive:

- Full keyboard navigation and remappable shortcuts.
- Respect system font scaling; ImGui needs explicit DPI handling.
- Colorblind-safe defaults for the UI chrome, since we will inevitably use color
  to encode property state.
