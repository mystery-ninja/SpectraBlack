# 16 — Third-Party Effect Plugins (`.aex` and OpenFX)

## The question

Can Spectra Black run the After Effects effect plugins people already own —
Video Copilot, Red Giant, Trapcode, Sapphire, and the rest?

This is a different problem from importing `.aep`
([`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md)), and conflating the two
leads to bad planning. A `.aep` is **data** that a user authored; parsing it is
ordinary engineering. An `.aex` is **a compiled program** written against Adobe's
private host API; running it means reimplementing After Effects' plugin host.

The short answer: **technically demonstrated, platform-limited to Windows, and
there is a better route to the same goal.**

## What an `.aex` actually is

A Windows DLL or macOS dylib (renamed) implementing Adobe's AE Effect SDK. It
expects a host that provides:

- the `PF_*` command selectors (`GLOBAL_SETUP`, `RENDER`, `SMART_PRE_RENDER`,
  `SMART_RENDER`, `SEQUENCE_SETUP`, and many more)
- dozens of callback **suites** — handles, world/pixel access, parameter
  management, iteration, sampling, paths, transforms
- AE's parameter model, with the host owning keyframes and interpolation
- AEGP suites for anything that reaches into the project itself

"Decompiling" an `.aex` does not help. Decompilation yields machine code, not the
ability to run it. What is required is **hosting**: providing the API surface the
plugin calls into. That is reimplementing a large, undocumented, mutating
interface.

## Where Adobe stands

Unambiguous, and quoted directly from their SDK guide:

> *"Adobe neither supports nor recommends the creation of Adobe-compatible
> third-party hosts. While it may be possible to create a partially functional
> host by reverse engineering from the plug-in API specification, we do not
> recommend it and will not support you in doing so."*

Adobe also notes that **no third-party host supports SmartFX or the AEGP
functions**, and SmartFX is the modern render path that current plugins depend on.
Premiere Pro — Adobe's *own* application — runs `.aex` files only partially, and
plugin authors must write explicit host-detection code to work in both. If Adobe
cannot make two of its own products fully agree, a third-party host's realistic
ceiling is "some plugins, some of the time."

## Prior art: it has been done

Worth knowing, because it moves this from "impossible" to "bounded but
constrained."

[`potistudio/aexlo`](https://github.com/potistudio/aexlo) (Rust, MIT, active) is
an AE plugin runtime emulator that loads and renders `.aex` plugins outside After
Effects — the author's analogy is Wine for AE plugins. It implements the SDK
interface and deliberately leaves host-dependent features for the integrator.

So the concept is proven. The constraint is in its own requirements.

## The blocker: platforms

> **`.aex` plugins are built for Windows x64 and macOS arm64. There is no Linux
> build, and there can never be an Android build.**

`aexlo` states this plainly: AE plugins target Windows and macOS only, so Linux is
not supported.

Against our platform matrix ([`02-TECH-STACK.md`](02-TECH-STACK.md)):

| Platform | `.aex` support possible? | Why |
|---|---|---|
| Windows | Yes | Native binaries load |
| Linux | No — would need a Wine-style shim | No Linux `.aex` exists to load |
| Android (arm64) | **Structurally impossible** | x64 Windows PE binaries on ARM; no vendor will ever ship one |

This breaks the premise the whole project rests on. [`01-ARCHITECTURE.md`](01-ARCHITECTURE.md)
is built so that one core runs everywhere; `.aex` hosting would be a
Windows-only feature producing projects that **cannot open on Linux or Android at
all** — not degraded, not slower, simply unopenable, because the effect does not
exist there.

A user's scene would stop being portable. That is a deeper cost than the
implementation effort, and it is the actual reason to decline rather than any
judgement about how valuable AE plugins are.

## The better route: OpenFX

The industry already solved this, and the solution is open.

**OpenFX (OFX)** is the standard plugin interface for 2D visual effects,
originated at The Foundry and now stewarded by the **Academy Software
Foundation** under **BSD-3-Clause**. It is what DaVinci Resolve, Nuke, Fusion,
Vegas, Flame, and Natron all speak.

| | `.aex` | OpenFX |
|---|---|---|
| Specification | Private, undocumented for hosts | **Open and authoritative** |
| License | Adobe SDK terms | **BSD-3-Clause**, GPL-compatible |
| Host support code | None — write it all | **Official `HostSupport` library provided** |
| Vendor stance | Actively discourages hosts | **Actively wants hosts** |
| Linux | No | **Yes** |
| Open-source hosts to learn from | None | Natron, TuttleOFX |
| Commercial effects available | Many | Many — Sapphire, Boris, Neat, Red Giant titles |

From the OpenFX project's own pitch to host authors: *"offering an OpenFX host
gives your users access to all the OpenFX plug-ins available, both commercial and
open source. No need to develop and maintain a custom API."*

The overlap matters for your friends specifically: **a significant share of the
big commercial effect packages ship OFX builds alongside their AE builds**, because
Resolve and Nuke are large markets. Supporting OFX gets a meaningful fraction of
the same effects, legally, with a documented API, on Linux and Windows both.

OFX does not cover Android either — it is a desktop plugin standard with native
binaries — but it degrades honestly: a desktop-only effect rather than a
Windows-only one, in an ecosystem that expects host capability to vary.

## Recommendation

1. **Ship our own `.sbx` effect format first.** Shader plus manifest, per
   [`13-EXTENSIBILITY.md`](13-EXTENSIBILITY.md). Cross-platform by construction,
   sandboxed, safe to share, and it works on Android. This is the only tier that
   serves every target.
2. **Add an OpenFX host in Phase 4**, if there is demand. Open standard, BSD
   licensed, official host support library, real commercial plugin availability,
   and open-source hosts to reference.
3. **Do not host `.aex`.** Not because the reward is small — it is genuinely large
   for AE users — but because it is Windows-only in a three-platform project, it
   breaks scene portability, Adobe will actively not help, and the modern SmartFX
   path that current plugins need is the part third-party hosts have never managed.

### If we ever revisit `.aex`

Conditions that would change the answer, written down so the decision can be
re-litigated with evidence rather than enthusiasm:

- A Windows-only build becomes acceptable for a clearly-marked subset of effects,
  **and** scenes using them degrade to placeholder nodes elsewhere
  ([`06-PROJECT-FORMAT.md`](06-PROJECT-FORMAT.md)) rather than failing to open.
- `aexlo` or similar matures to cover SmartFX and proves out against the specific
  plugins our users actually own.
- Someone volunteers to own it as an **optional, separately-distributed module**,
  keeping the legal and maintenance exposure out of the core.

That last point is the realistic shape if it ever happens: a plugin bridge is a
separate project that integrates with us, not a feature of the core app.

## What AE users actually lose

Honest accounting, since this decision is the one most likely to disappoint the AE
side:

- **Their projects open.** Compositions, layers, transforms, keyframes, nesting,
  text, markers — [`12-AEP-COMPATIBILITY.md`](12-AEP-COMPATIBILITY.md).
- **Their audio-reactive work gets better**, not worse. The baked-amplitude
  workaround is replaced by live drivers
  ([`05-ANIMATION-MODEL.md`](05-ANIMATION-MODEL.md)).
- **Their third-party effects do not come with them.** Blur, glow, color grading,
  RGB split, displacement and the rest we implement ourselves; a specific
  Trapcode or Sapphire look would need rebuilding, or OFX if that vendor ships it.

The last point is a real loss and should be stated plainly in any migration guide
rather than discovered. But it applies to every compositor that is not After
Effects — Resolve and Nuke users accept exactly the same trade, which is precisely
why OFX exists.

## Open decisions

1. **Confirm no `.aex` hosting**, on the portability grounds above rather than on
   effort.
2. **OpenFX host: Phase 4, or never?** Recommendation is Phase 4 behind demand.
   It is a substantial amount of work (image fetching, parameter mapping, threading,
   GPU render support) and should not be committed to on speculation.
3. **Should `.sbx` deliberately borrow OFX's parameter model**, so an OFX bridge
   later is a mapping job rather than a redesign? Low cost now, possibly high value
   later.
