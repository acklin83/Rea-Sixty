# Rea-Sixty vs. SSL 360° — Why We're Finishing This

This is the plain-language version. The architecture deep-dive lives in
[`architecture-decision.md`](architecture-decision.md), the legal
write-up in [`interop-rationale.md`](interop-rationale.md). This one is
for anyone who walks up and asks "wait, SSL already ships software for
this — why are you writing your own?"

## The short answer

SSL's software works. It is also built for a different audience: people
who use Pro Tools, Logic, Cubase, Live, or Studio One — usually one of
those at a time, often with an SSL channel-strip plug-in on every
track. For that audience, SSL 360° is fine.

REAPER users are a different shape. We have big sessions. We colour
tracks. We do not put an SSL plug-in on the drum overheads. And the
moment you try to use SSL 360° the way a REAPER user actually works,
three things start grinding:

1. **Track colours only show up if you load an SSL plug-in on every
   single track.** This is not a setting you can flip. The colour lives
   inside the SSL plug-in, not in the DAW. A 200-track session would
   need 200 plug-in instances just to get colours on the scribble
   strips. That is not a tradeoff, that is a wall.
2. **SSL 360° grabs the controller and won't share it.** While SSL's
   software is running, nothing else can talk to UF8 or UC1 over USB.
   No "small helper extension" can sit alongside it and push colours.
   Either you replace SSL 360° or you live without colours.
3. **The bridge between REAPER and SSL 360° is MCU.** MCU is the
   ancient Mackie protocol every DAW understands. It has no concept of
   colour. It has 14-bit faders instead of REAPER's native 16-bit. It
   can't tell the controller what's on screen or which plug-in
   parameter you're touching. SSL has to use it anyway because it's the
   only thing that works with all five DAWs they support.

None of these are bugs. They're the natural shape of "one piece of
software that has to work with everyone." SSL is not going to fix them,
because fixing them would mean abandoning the multi-DAW design that
makes 360° worth shipping in the first place.

## What we built instead

Rea-Sixty is a single REAPER extension. You drop one file into your
REAPER install, and the controllers light up. There is no background
daemon, no virtual MIDI port to set up, no plug-in to load on every
track. REAPER talks to the extension through its own C API — the same
API REAPER's own mixer uses. The extension talks to the UF8 and UC1
directly over USB.

In one picture:

```
SSL 360°  : REAPER ─MCU MIDI─ SSL360Core (daemon) ─USB─ UF8/UC1
Rea-Sixty : REAPER ─C API─ extension (in REAPER) ─USB─ UF8/UC1
```

Every middleman in the top line is a middleman the bottom line doesn't
need. Each removed hop is one fewer thing that can drop frames, lose
state, or get the colour wrong.

## What changes for a REAPER user

The big one: track colours work everywhere, all the time. You colour
tracks in REAPER like you always have. They appear on the UF8 scribble
strips. No plug-in required, no special mode, no setup. This is the
feature that started the whole project, and it's been working since
Phase 1.

The faders get smoother. REAPER stores volume in 16-bit; MCU only
carries 14-bit. By talking to the API directly we keep the full
resolution. Same for V-Pots — REAPER gets the raw delta, not a
quantised MIDI CC.

The UC1's gain-reduction meter follows the actual compression on the
focused track. SSL 360° does this through its own internal IPC; we do
it through a standard REAPER mechanism (`GainReduction_dB`, the PreSonus
VST3 extension REAPER already exposes). Same result, no SSL plug-in
required.

And then there are the features SSL 360° simply does not have, because
they only make sense if you know REAPER's data model:

- **Folder Mode** — the bank shows folder parents, you long-press SEL
  to expand a folder into its children.
- **Selection Sets** — eight saved track selections per project,
  recalled with one button.
- **Show Sends / Show Receives** — pick a track, the eight strips
  become its eight sends.
- **Generic FX Learn** — bind any VST or JS plug-in parameter to any
  V-Pot or button. Not just SSL plug-ins.
- **An on-screen Plug-in Mixer that picks up your REAPER theme.** Not
  a separate window in a separate app with SSL's own UI; a docked
  panel inside REAPER, in the colours of whatever theme you use.
- **Settings inside REAPER too.** Same dockable window, no second app.

All of these are in `ROADMAP.md` under Phase 2.5–2.7. Some are shipped,
some are landing. None require any more reverse engineering — the hard
USB protocol work is done.

## Why not just wait for SSL to fix it

SSL knows what colours-without-plug-in would mean. They have not built
it because to build it they'd have to read DAW state without going
through their own plug-in — which means either an integration per DAW
or an entirely different bridge. They support five DAWs. Building five
custom integrations is expensive. Building one MCU bridge is cheap. So
they built the MCU bridge, and the colour feature stays gated behind
their own plug-in.

We only have to make one DAW work. That's the entire reason this
project exists: a REAPER-only tool can use REAPER's API. SSL cannot.
That is not a contest SSL is going to enter.

## What it costs to finish

The expensive half is already paid. Decoding the UF8 and UC1 USB
protocols by passively watching SSL 360° drive the hardware (legally —
see `interop-rationale.md`) took months. That part is in
`protocol-notes.md` and `protocol-notes-uc1.md`, with checksums verified
and init sequences working.

What's left is REAPER-API plumbing and a docked UI window. Three
phases, each broken into small steps in `ROADMAP.md`. No more sessions
with Wireshark on a Windows machine. No more "what does this byte
mean." Just REAPER calls and ImGui widgets — the kind of work this
codebase is already shaped for.

## The honest caveats

**Firmware updates still go through SSL 360°.** SSL ships firmware as
opaque blobs; we don't reverse-engineer firmware. Once every few years
you'll keep SSL 360° around long enough to apply an update. Acceptable.

**Running unofficial code on your controller may void the warranty.**
Documented in the README. If warranty preservation matters more than
the features above, don't run Rea-Sixty. This is a user choice, not
something we can fix.

**The legal posture.** We're on solid ground (EU Software Directive
Art. 6, §69e UrhG, 17 USC §1201(f), nominative trademark use, no SSL
binaries redistributed). There's also a drafted email to SSL ready to
send if collaboration ever becomes possible
([`outreach/ssl-email-draft.md`](outreach/ssl-email-draft.md)). The
preferred outcome is friendly coexistence; the fallback is just to
keep shipping what we ship.

**If SSL ever removes the plug-in-on-every-track requirement,** the
headline feature parity closes. We still have folder mode, selection
sets, generic FX learn, the themed mixer, the in-REAPER settings, the
16-bit faders, the no-daemon install. The project keeps its reason
to exist.

## So why finish it

Because the controllers are good hardware, the gap they have today is
fixable, and we've already fixed most of it. Stopping now means
throwing away working code to go back to a setup that needs a plug-in
on every track and still won't give you the features REAPER users
actually want.

Durchziehen.
