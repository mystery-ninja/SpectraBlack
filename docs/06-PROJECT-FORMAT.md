# 06 — Project Format

## Requirements

1. **Human-readable and diffable.** This is a public repo with contributors;
   scene files will end up in git, in bug reports, and in pull requests. A binary
   format makes all of that worse.
2. **Forward and backward tolerant.** An old build opening a new file should
   degrade gracefully, not crash.
3. **Shareable as one file**, including assets, because preset sharing is how
   Avee's community actually works.
4. **Fast to load.** Under 100 ms for a typical project, excluding media decode.

## Two forms

| Form | Extension | Contents |
|---|---|---|
| Working project | `.napproj` | JSON, assets referenced by relative path |
| Shareable bundle | `.napbundle` | Zip containing the JSON plus embedded assets |

Working projects stay text so they diff cleanly. Bundles are for distribution,
and are just a zip, so anyone can open one and look inside — which matters for a
community that will want to learn from each other's scenes.

Analysis caches are **never** stored in either. They are derived data, keyed by
audio file hash, and live in a cache directory.

## Structure

```jsonc
{
  "format": "newapp-project",
  "formatVersion": 1,
  "appVersion": "0.1.0",

  "project": {
    "name": "My Visualizer",
    "resolution": [1920, 1080],
    "frameRate": 60,
    "duration": 222.5,
    "colorSpace": "linear-srgb"
  },

  "audio": {
    "source": "assets/track.mp3",
    "analysis": {
      "bandCount": 256,
      "bandScale": "mel",
      "attack": 0.08,
      "decay": 0.35,
      "normalization": "track"
    },
    "beatGrid": {
      "bpm": 128.0,
      "offset": 0.213,
      "manualOverrides": []
    }
  },

  "compositions": [
    {
      "id": "main",
      "name": "Main",
      "resolution": [1920, 1080],
      "nodes": [
        {
          "id": "n_logo",
          "type": "Image",
          "name": "Logo",
          "enabled": true,
          "blendMode": "Alpha",
          "source": "assets/logo.png",

          "properties": {
            "position": {
              "static": [0.5, 0.4],
              "keyframes": [
                { "t": 0.0, "v": [0.5, -0.2], "out": "EaseOut" },
                { "t": 1.8, "v": [0.5,  0.4], "in":  "EaseOut" }
              ],
              "drivers": [
                {
                  "signal": "beat",
                  "blend": "Add",
                  "amount": 0.04,
                  "shaping": { "random": { "seed": 7 }, "decay": 0.25 }
                }
              ]
            },
            "scale": {
              "static": 1.0,
              "drivers": [
                { "signal": "beatPulse", "blend": "Multiply", "amount": 0.15 }
              ]
            },
            "opacity": { "static": 1.0 }
          }
        }
      ]
    }
  ]
}
```

### Why properties are objects, not bare values

`"opacity": { "static": 1.0 }` is more verbose than `"opacity": 1.0`, and the
verbosity buys uniformity: the reader never needs to handle two shapes, and
adding a driver to a property is not a structural change to the file. Since the
property system is generated from the declarative registry, serialization is
automatic and the verbosity costs nobody any effort.

A property with only a static value does omit the empty `keyframes` and `drivers`
keys, so simple scenes stay readable.

## Versioning

`formatVersion` is an integer that increments on breaking changes. Migrations are
a chain of functions:

```cpp
// Each migration takes version N to N+1.
registerMigration(1, 2, [](json& j) { /* ... */ });
```

Unknown keys are **preserved, not dropped**, when loading and re-saving. An old
build opening a new project keeps the new fields it does not understand, so a
round-trip through an older version does not silently destroy someone's work.
This is cheap to implement now and nearly impossible to retrofit.

Unknown *node types* become a visible placeholder node that preserves its
properties verbatim, rather than vanishing.

## Asset references

Relative paths from the project file, always with forward slashes, resolved
against the project directory. Absolute paths are rejected on load since they
break the moment a project is shared.

Missing assets produce a placeholder plus a relink prompt rather than a load
failure. Projects that cannot open because one PNG moved are a reliable source of
user fury.

Each asset also records a size and content hash, so we can detect "this is the
same file, moved" and offer to relink automatically.

## Autosave and recovery

- Autosave every 60 seconds to `<project>.autosave`, atomically: write to a temp
  file, `fsync`, then rename.
- Keep the last 10 autosaves in a rotating set.
- On crash, offer recovery on next launch.

Atomic rename matters. A half-written project file from a crash during save is
the worst possible failure mode for a creative tool, and the temp-then-rename
pattern makes it structurally impossible.

## Undo

Not serialized, but worth specifying here since it constrains the data model: a
command stack of inverse operations, not scene snapshots. Snapshots are simpler
but make undo memory scale with scene size, and a 500-step history of a large
scene gets expensive.

```cpp
struct Command {
    virtual void apply(Scene&) = 0;
    virtual void revert(Scene&) = 0;
    virtual bool mergeWith(const Command&) = 0;  // coalesce slider drags
};
```

`mergeWith` is what stops a single slider drag from producing 200 undo steps.
