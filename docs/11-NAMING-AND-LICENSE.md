# 11 — Naming and License

Two decisions that are cheap to make now and expensive to change later. Both are
open for discussion with contributors.

---

# Part 1 — The name

## The brief

The name takes the form **`<Name> Black`**, where "Black" is a suffix meaning
*complete* — every feature present, designed well, nothing missing. Not
"perfect" in the naive sense, but the sense of a black belt: mastery, finished
work, nothing left to prove.

That is a good instinct, because black has a second meaning that fits this app
exactly: **black is what a visualizer renders onto.** An empty composition is
black. Light, color, and motion appear out of it. The name can mean both
"complete" and "the void the visuals come from" at once.

The `<Name>` half should carry the *light* or *spectrum* idea, since the app is
first and foremost a visualizer. A good pair reads as "light, perfected."

### The Lumen problem

Worth stating up front since it is the obvious candidate: **Lumen is Unreal
Engine 5's global illumination system.** Inside graphics software specifically
that collision is severe — every search result in our own field belongs to Epic.
`Lumina`, `Luma`, and `Lux` are all close to the sound you liked without
inheriting that problem.

## Candidates

### Spectra Black — recommended

A spectrum is simultaneously **a band of light** and **the frequency content of
audio**. It is the one word that means both halves of this app at once, and
"spectrum analyzer" is literally what the core of the renderer is.

- Says *visualizer* to anyone technical without needing explanation
- The light meaning and the audio meaning are both correct, not a pun
- "Spectra" is the plural, which sounds better than "Spectrum Black" and hints at
  many visualizations rather than one
- Reads as a premium product rather than a hobby project
- Clean in software namespace; no major collision in graphics or audio

### Prism Black

A prism takes white light and splits it into a visible spectrum — a physical
object that performs exactly our function. The strongest candidate on pure
imagery, and the easiest to build a logo and icon from, which matters more than
it sounds for an app people discover visually.

Slightly more common as a software name than Spectra, but no collision serious
enough to rule it out.

### Lumina Black

The closest to the *Lumen* sound you liked, without Epic's shadow. Means light,
feels polished, pairs well with Black. Weaker than Spectra or Prism in that it
says nothing about audio or spectrum — it could be any graphics tool.

### Luma Black

**Luma** is the brightness component of a video signal, the counterpart to chroma.
A genuinely well-chosen technical term for video software, and very short. The
drawback is that it is *so* specific to video encoding that it may read as a codec
utility rather than a creative tool.

### Flux Black

Pleasing double meaning: **radiant flux** is a measure of emitted light, and
**spectral flux** is the exact algorithm we use for beat detection (see
[`04-AUDIO-ENGINE.md`](04-AUDIO-ENGINE.md)). Short, punchy, confident. "Flux" is
fairly well-used in software generally, though not in this space.

### Aurora Black

An aurora is nature's own light show — large, colorful, and moving. Lovely
imagery and immediately evocative. Some existing software use (Aurora Store,
several databases), and it leans pretty rather than powerful.

### Lunar Black

You mentioned it and the sound is good. The reservation is semantic: the moon
*reflects* light rather than emitting it, which is a slightly passive metaphor for
an app that generates visuals. Strong night and music association though.

### Luminous Black

Accurate but structurally awkward — two adjectives in a row, with no noun for
either to attach to. "Luminous" also runs long for a logo.

## Recommendation

**Spectra Black**, because "spectrum" is the rare word that means light and audio
at the same time, and that is precisely the intersection this app occupies.
**Prism Black** is a close second and the better choice if you care more about
the logo than the double meaning.

## Checks before committing to a name

1. GitHub organization and repository name available
2. Domain availability
3. A trademark search in software and entertainment classes
4. It survives being said out loud in a video, and typed from memory

---

# Part 2 — The license

## What you asked for

> Modifying this app is okay, as long as the original app creators are credited
> and the modified version is not made to make profit in any way. I don't gain
> money from this project either and I don't want money, I just want to help
> people.

That is a clear and generous intent, and it maps onto three separate
requirements:

1. **Attribution** — original creators must be credited
2. **Modification allowed** — people can fork and change it
3. **No commercial exploitation** — nobody profits from it

Requirements 1 and 2 are easy and every open license handles them. Requirement 3
is the one with a consequence worth understanding before you choose.

## The problem with "non-commercial"

A license that forbids commercial use is **not an open source license.** This is
not a technicality or an opinion — it fails the Open Source Definition and the
Free Software Definition, both of which specifically require no restriction on
field of use. The practical fallout:

- **Linux distributions cannot ship it.** Debian, Fedora, Arch's main repos, and
  F-Droid's main repository all require genuinely free licenses. For an app whose
  primary desktop platform is Linux, that directly blocks the easiest way for
  people to install it.
- **Many contributors will not contribute.** A meaningful share of open source
  developers have a personal policy against non-commercial licenses, partly
  because they cannot use the result in their own work.
- **"Commercial" is dangerously vague.** It has barely been tested in court, and
  reasonable people disagree about whether these count:
  - A YouTuber monetizing a video they made with the app
  - A streamer running it on a channel with subscriptions
  - A wedding videographer using it for a paid client
  - A company using it for internal presentations
  - A school charging tuition

  You would be fielding that question forever, and every answer you give is a
  guess about what a court would say.

Here is the uncomfortable part: **a non-commercial clause would forbid the exact
people you most want to help.** The small creator trying to make a living from
music videos is, by most readings, a commercial user.

## What you probably actually want

Read your intent again. The thing being guarded against is not "someone earns
money." It is **someone taking your work, closing it up, and exploiting it
without credit.** Those are different, and the second one is precisely what
copyleft was invented to prevent.

### GPL-3.0 — recommended

- **Credit is mandatory.** Copyright notices and attribution must be preserved.
- **Modification is allowed**, which you want.
- **Anyone distributing a modified version must release their source**, under the
  same license. This is the key clause: a company *cannot* take Blackbody, polish
  it, and sell it as closed software. The thing you are actually worried about is
  structurally impossible.
- It stays free for everyone, forever. Nobody can take it away from the community,
  including a future version of you.
- Distributions can ship it. F-Droid can ship it. People can actually install it.
- Contributors are comfortable with it.

The honest gap: GPL does not forbid *selling* it. Someone could charge money for
GPL binaries. In practice this almost never matters, because they must also give
every buyer the source under the GPL, which collapses the business model
immediately. And anyone can undercut them by giving it away free — as we will be.

### AGPL-3.0 — consider if worried about cloud

Identical to GPL plus a clause covering software run over a network. Given that
this is explicitly a local desktop and mobile tool with no cloud ambitions, the
extra clause mostly adds friction without protecting anything. **GPL-3.0 is the
better fit.**

### If you truly want non-commercial anyway

Then do not use Creative Commons. **CC BY-NC-SA is not built for software** — it
has no patent grant, no warranty disclaimer suited to code, and Creative Commons
themselves [recommend against using it for software](https://creativecommons.org/faq/#can-i-apply-a-creative-commons-license-to-software).

The correct choice would be **PolyForm Noncommercial 1.0.0**, which is a properly
drafted non-commercial license written specifically for software by actual
software lawyers. If the NC restriction is non-negotiable for you, use that one
and accept the distribution and contributor consequences knowingly.

## Decision: GPL-3.0

**Settled.** The full text is in [`LICENSE`](../LICENSE) at the repository root.

It delivers the credit you asked for, it makes closed-source exploitation
impossible, it keeps the project genuinely free and installable from any Linux
distribution, and it does not accidentally lock out the independent creators you
built this for.

If the reaction is "but then someone could still sell it" — that is true, and it
is worth less than it sounds. They would have to hand every customer the full
source code with the right to redistribute it for free. Nobody builds a business
on that.

### What this means in practice

- Every source file gets a short header pointing at `LICENSE`.
- Contributors keep copyright over their own work; there is no CLA and no
  copyright assignment. This is deliberate — it means no single party, including
  the project founder, can ever relicense the work out from under everyone.
- Dependencies must be GPL-compatible. This rules out any proprietary library and
  is worth checking before adding anything new. Our current picks (GLFW, GLM,
  Dear ImGui, miniaudio, stb, PFFFT, FreeType, HarfBuzz, nlohmann/json) are all
  permissively licensed and therefore fine.
- **FFmpeg needs care.** It is LGPL by default but becomes GPL when built with
  `--enable-gpl`, and some codecs carry further restrictions. Since we are GPL,
  this is permitted, but it must be documented in the build so distributors know
  what they are shipping.

## Why this must be settled early

Relicensing a project requires permission from **every single contributor** who
has ever had code merged. Once other people start committing, changing the
license goes from a one-line edit to a months-long process of tracking down
contributors, some of whom will have vanished.

Until a `LICENSE` file exists, the repository is legally "all rights reserved"
by default. Nobody can safely contribute, because they have no license to the
work they would be building on. **Add the license before adding contributors.**
