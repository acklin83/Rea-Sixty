# Rea-Sixty vs. SSL 360° — Why We Stay the Course

Companion to [`architecture-decision.md`](architecture-decision.md) (the
internal-architecture argument for dropping CSI) and
[`interop-rationale.md`](interop-rationale.md) (the legal basis). This
document is the **product** argument: a side-by-side of what SSL 360°
gives a REAPER user vs. what Rea-Sixty gives them, and why finishing
Rea-Sixty is the better outcome even though SSL's software already
exists.

## TL;DR

SSL 360° is a competent generic surface bridge built for a multi-DAW
audience. For a REAPER user it has three structural problems that no
amount of SSL engineering effort will fix, because they are consequences
of SSL's product brief, not bugs:

1. **Track colours only render with an SSL plug-in on every track.**
   Unworkable above ~30 tracks; impossible above 100.
2. **The vendor-USB interface is claimed exclusively.** No coexistence,
   no third-party extension, no DAW-side colour push.
3. **MCU/HUI is the bridge to REAPER.** 14-bit faders, no colour
   channel, no FX-parameter feedback, no GR meter forwarding.

Rea-Sixty removes all three because it replaces SSL 360°'s host role
end-to-end rather than layering on top of it. The cost of that
replacement is already paid (Phase 1 + 2 shipped). Walking away now
means burning that cost and keeping the three structural problems.

## Architecture: side by side

| Layer | SSL 360° + REAPER | Rea-Sixty |
|---|---|---|
| Host process | SSL360Core (always-on daemon) + SSL 360° UI | `reaper_uf8` extension inside REAPER |
| DAW bridge | MCU/HUI over virtual MIDI ports | REAPER C API directly (`csurf_inst`, `TrackFX_*`, `CSurf_On*`) |
| Surface transport | Vendor-USB (exclusive claim) | Vendor-USB via libusb |
| Plug-in mixer rendering | Inside SSL 360° (Plug-in Mixer page) | Inside REAPER (dockable ImGui window, Phase 2.6) |
| Config | SSL 360° XML, per-DAW layer tabs | JSON, REAPER-only, GUID-keyed FX bindings |
| Coexistence with the other host | Mutually exclusive (SSL 360° holds USB) | Same exclusivity unavoidable — but we are the one host |

Topology in one line:

```
SSL 360°  : REAPER ─MIDI─ SSL360Core ─USB─ UF8/UC1
Rea-Sixty : REAPER ─C-API─ extension ─USB─ UF8/UC1
```

Every hop SSL 360° needs is a hop we delete.

## Feature matrix

Status legend: **✓** working today on `main`. **◐** planned in
`ROADMAP.md` with a concrete phase. **✗** structurally unsupported.

### Colours on the UF8 scribble strips
| | SSL 360° | Rea-Sixty |
|---|---|---|
| Track colours in the DAW layer (channel/MCU) | ✗ — not supported at all | ✓ |
| Track colours in Plug-in Mixer mode | ✓ — requires an SSL plug-in on every track | ✓ — no per-track plug-in required |
| Colour source | SSL Channel Strip "Track Colour" parameter | REAPER `GetTrackColor()` |
| Update on REAPER colour change | ✗ (the SSL plug-in stores its own colour) | ✓ (polled in `Run()`, pushed on change + bank shift) |

This is the headline gap. SSL's design assumes the channel strip is the
source of truth; REAPER users keep colour in REAPER. No amount of SSL
plug-in coverage maps a 200-track session because mid-stage tracks
(buses, FX returns, dialogue stems) carry no SSL plug-in by choice.

### Fader & V-Pot resolution
| | SSL 360° | Rea-Sixty |
|---|---|---|
| Fader resolution | 14-bit (MCU pitch-bend) | 16-bit (`CSurf_OnVolumeChange`) |
| V-Pot resolution | 7-bit MCU CC | Native delta to REAPER API |
| Touch state | MCU note-on/off | Direct `IReaperControlSurface::GetTouchState()` |
| Feedback loops | Virtual MIDI loop susceptible | `ignoresurf=g_surface` cuts the loop at source |

### UC1 / SSL plug-in integration
| | SSL 360° | Rea-Sixty |
|---|---|---|
| UC1 controls SSL plug-in on focused track | ✓ via VST3 IPC | ✓ via REAPER `TrackFX_*Normalized` |
| GR meter on UC1 | ✓ via SSL's own probe | ✓ via the PreSonus VST3 GR extension (`GainReduction_dB`) REAPER already exposes — no JSFX, no sidechain tap |
| Combo / 360° Link recognition | ✓ | ✓ (`PluginMap`, see `concepts.md`) |
| Third-party plug-in parameter mapping | ✗ | ✓ generic FX Learn (Phase 2.5d, shipped) |

### REAPER-native features SSL 360° cannot expose
| | SSL 360° | Rea-Sixty |
|---|---|---|
| Folder Mode on the bank (parents/children) | ✗ | ◐ 2.5a |
| Selection Sets (8 GUID-keyed slots) | ✗ | ◐ 2.5b |
| Show Sends / Show Receives on the bank | ✗ (sends layer only) | ◐ 2.5c |
| Generic FX learn (any VST/JS/AU) | ✗ | ✓ 2.5d |
| Plug-in Mixer themed to REAPER's theme | ✗ | ◐ 2.6 |
| Settings UI inside the DAW window | ✗ (separate app) | ◐ 2.7 |
| Foot-switch bindings | ✓ | ◐ 2.7b |
| Always-Fine Pan / Sends, Show Auto State | ✓ | ◐ 2.7d |
| Identify Unit (LCD-flash) + Drag-to-Reorder | ✓ | ◐ 2.7a |
| Diagnostic Export `.zip` | ✓ | ◐ 2.7a |

Gap analysis source: [`ssl-360-settings-inventory.md`](ssl-360-settings-inventory.md).
The features we **don't** copy (3-DAW Layer tabs, Transport Master,
XML profile interop, in-app firmware update) are listed there with
rationale. They are SSL-multi-DAW concessions; a REAPER-only tool gets
to be simpler.

### Operational
| | SSL 360° | Rea-Sixty |
|---|---|---|
| Background daemon required | ✓ SSL360Core | ✗ — in-process with REAPER |
| Virtual MIDI port setup | ✓ required | ✗ |
| Per-track SSL plug-in for colours | ✓ required | ✗ |
| Install footprint | SSL 360° installer + driver + plug-ins | One `.dylib` / `.dll` drop |
| Cross-platform path | macOS + Windows (vendor's choice) | macOS today; Windows + Linux on the libusb path (Phase 4) |

## The structural argument

SSL 360° cannot become what we want because the things missing are
not features SSL forgot — they are downstream of SSL's product
choices:

- **Plug-in-on-every-track for colours** is downstream of "the SSL
  plug-in owns the channel strip state". Removing that requirement
  means SSL 360° has to read DAW state without going through its own
  plug-in. That is the REAPER C API path. That is what Rea-Sixty is.
- **MCU/HUI as the DAW bridge** is downstream of "support every DAW
  with one codebase". MCU has no colour channel and no parameter
  feedback. As long as SSL targets Pro Tools / Logic / Cubase / Live /
  Studio One simultaneously, MCU is the only sane lowest common
  denominator. A REAPER-only tool can use REAPER's API instead.
- **Exclusive USB claim** is downstream of "one host owns the
  controller". That constraint stays; the question is which host. With
  Rea-Sixty installed, REAPER owns it — directly, with full state.

These are not features SSL is going to ship in a 360° point release.
They require giving up the cross-DAW abstraction. SSL has no commercial
reason to do that. We do.

## What "durchziehen" (finishing it) actually costs

Phases 1 and 2 are in. The work that remains (from `ROADMAP.md`):

- **Phase 2.5** — Folder Mode, Selection Sets, Send/Receive, Generic FX
  Learn. REAPER-API work, no new captures needed.
- **Phase 2.6** — Plug-in Mixer window (ImGui inside the existing
  dockable SWELL window).
- **Phase 2.7** — Settings tab, also inside that window.
- **Phase 4** — Windows + Linux ports, more controllers.

None of this needs more reverse engineering of the existing devices.
The vendor-USB surface for UF8 + UC1 is decoded and committed to
`protocol-notes.md` / `protocol-notes-uc1.md`. The expensive part
(capture, decode, init-sequence replay, init-state correctness) is
done. The remaining work is REAPER-API plumbing and UI — the things
this codebase is already structured for.

## Risks honestly noted

- **Firmware updates.** SSL still ships firmware blobs through SSL 360°.
  Users keep SSL 360° installed for that one task (Phase 4 non-goal).
  Acceptable: firmware updates are an hour every few years.
- **Warranty posture.** Documented in `README.md`. Anyone needing
  warranty preservation should not run Rea-Sixty. That is a user
  choice, not a project decision.
- **Legal posture.** Covered in `interop-rationale.md` — passive USB
  observation of legally licensed software, EU Software Directive Art.
  6, §69e UrhG, 17 USC §1201(f). No SSL binaries, firmware, or
  trademarks redistributed. We have an SSL outreach draft
  ([`outreach/ssl-email-draft.md`](outreach/ssl-email-draft.md))
  prepared if collaboration becomes possible.
- **SSL ships a 360° feature that closes the colour gap.** They would
  have to remove the per-track plug-in requirement. If that happens we
  gain the Plug-in Mixer parity feature; we still have everything in
  the "REAPER-native features SSL 360° cannot expose" row. Net effect
  on project value: small.

## Conclusion

The colour-on-DAW-layer headline feature is the easy half of the
argument. The harder half is that we are not building a parallel SSL
360° — we are building **the REAPER-shaped version of a UF8/UC1 host**,
which is structurally different and structurally better for this
audience. The work to get here is sunk; the work to finish (Phases
2.5–2.7) is REAPER-API plumbing, not reverse engineering. Stopping now
gives back nothing and keeps the three structural problems intact.

Durchziehen.
