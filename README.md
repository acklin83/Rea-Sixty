# Rea-Sixty

*REAPER × SSL UF8 / UC1 / UF1. A native, open-source replacement for SSL 360° on REAPER.*

> **Still stuck on v0.5.0 under Windows?** That build stopped REAPER from
> starting, so ReaPack is out of reach. Renaming one file gets you back in:
> [Windows recovery, thirty seconds](docs/v0.5.0-windows-recovery.md). Fixed
> since v0.5.1; macOS and Linux were never affected.

> [Released](https://github.com/acklin83/Rea-Sixty/releases). Install via ReaPack:
> ```
> https://github.com/acklin83/reaper-scripts/raw/main/index.xml
> ```
> (The ReaPack index lives in a separate `reaper-scripts` repo, which is expected and not a typo.)
> Or download the manual installer from the [latest release](https://github.com/acklin83/Rea-Sixty/releases/latest) (Mac / Windows / Linux archives).

Rea-Sixty drives the SSL **UF8**, **UC1** and **UF1** controllers directly from a REAPER extension over their vendor-USB protocols. No SSL 360° required, no plugin-on-every-track restriction, no CSI, no virtual MCU MIDI. Track colors land on the UF8 scribble strips in the DAW layer (something SSL 360° does not offer at all), faders run at full 16-bit precision, and the SSL Bus Compressor's GR meter on the UC1 follows audio-driven gain reduction on the focused REAPER track.

## Status

Public beta. Mac (Apple Silicon **and** Intel), Windows (x64) and Linux (x86_64) all ship in the [latest release](https://github.com/acklin83/Rea-Sixty/releases/latest). macOS is the primary daily-driver. Phase 1 (UF8 standalone), Phase 2 (UC1 integration with parameter mirror + GR pipeline), Phase 2.5 (Folder Mode, Send/Receive, generic FX Learn, with Selection-Set storage still queued), Phase 2.7 (in-app Settings: Devices, Appearance, Behaviour, Bindings, Modes incl. REC + RME (TotalReaper) and DynaMount, FX Learn, Favourites, Selection Sets, Parameter Groups, Exchange, Manual, About) Phase 2.8 (Nav Mode for Markers + Regions) and Phase 3 (UF1) are shipped. v0.5.0 is the release where the UF1 became a full surface: colour screen, channel strip, plug-in mode, meter view, soft-key banks, Jog Mode with razor and envelope editing, its own FX-Learn layer and an Extender mode. v0.5.3 turned every soft-key bank into two full banks, Plain and Shift, and gave the UF1's nav cross a separate binding per jog object. v0.5.4 made REC a mode on the UF1, with the RME preamp on the pot above the fader, and taught the Settings window to hide the surfaces you do not own. Phase 2.6 (on-screen Plug-in Mixer view) is still pending; the docked window currently hosts the Settings tabs only. As of v0.4.0 the project also has a web home, [reasixty.com](https://reasixty.com), and a **Mapping Exchange** for browsing, installing and publishing plug-in mappings from inside REAPER or in the browser. Full feature reference in [`docs/user-manual.md`](docs/user-manual.md).

## What it does today

- **Direct REAPER ↔ UF8 / UC1 / UF1** via `csurf_inst` and libusb. No CSI, no virtual MIDI, no MCU.
- **UF1 as a full surface** (v0.5.0). Channel, DAW and plug-in views on its colour screen; the four V-Pots drive the focused SSL channel strip or a standalone Bus Comp, with SSL's own EQ curve drawn on screen. A meter view carries Overview bargraphs, the analogue VU and PPM needles, the goniometer, the RTA spectrum and the Loudness pages, fed live from the SSL 360° plug-ins. The UF1 can also act as the 9th strip of the UF8 bank, for tracks or for sends.
- **Jog Mode** on the UF1. The wheel has an object (Playhead, Scrub with audible scrubbing, Items, Envelope or Razor) and the arrow cross selects while the wheel moves, so you never let go of the wheel to change what you are working on. Shift is fine, Cmd copies, Ctrl is the cross axis. The five cross keys hold a separate binding **per object**, so the same key can do five different things, and Shift on them is a binding of its own rather than a hidden check. Item and razor drops trim behind and crossfade on release, following REAPER's own editing preferences rather than a policy of ours. Razor edits also work on envelope lanes, across several lanes and across tracks, and stay non-destructive until you let go.
- **DAW-layer track colors** on the UF8 scribble strips, polled from `GetTrackColor()` and pushed on bank shifts.
- **Full vendor-USB host responsibility**: init-sequence replay on open, scribble text + value zones, color bars, button/V-Pot/fader/fader-touch routing, LED feedback (solo / mute / select / arm / transport / automation), meter bands, layer management, heartbeat.
- **UC1 parameter mirror**: physical knobs follow the focused track's SSL Bus Compressor / Channel Strip in real time, with values mirrored back to the UC1 displays.
- **UC1 GR display driven by the SSL plug-in itself.** The compressor's reduction comes from `TrackFX_GetNamedConfigParm("GainReduction_dB", …)`, the PreSonus VST3 GR extension REAPER exposes for the SSL Bus Compressor / Channel Strip. That API carries a single figure per plug-in and none for the **gate**, so the Gate strip instead reads the SSL plug-in's own meter stream: Rea-Sixty can stand in for SSL 360°Core and receive it directly (opt-in via ExtState `rea_sixty/ssl_core`; SSL 360° must be quit, as the two share the same ports). The gate reduction shows on both the UC1 gate strip and the UF8's second GR row, and follows the focused track's channel strip. No JSFX probe, no sidechain tap.
- **UC1 input meter, true-input mode**: by default the input meter reads pre-FX samples via `AudioAccessor` (recorded/playing material only). Drop the bundled **"Rea-Sixty Input Level"** JSFX onto a track (auto-installed into REAPER's `Effects/Rea-Sixty/` folder on load) and the meter instead shows the level flowing *into* that probe: a live armed/monitored hardware input, an upstream plug-in's output, or a media source, live even while the transport is stopped. Detection only: remove the probe and the meter falls back to the `AudioAccessor` path.
- **Selection Modes**: DAW, Send, Pan, Plug-in Mixer, Folder Mode, Show-Only-Selected, and DynaMount (drives DynaMount robotic mic stands over the LAN); switched from the surface or from the Settings window.
- **Bindings system**: per-strip, transport, global, soft-keys per layer; modifier combos (incl. double-click latch on Shift); 12 user-defined Soft-Key Banks (8 buttons each) plus the 6 SSL stock banks, each bank carrying **two full sets of keys** (Plain and Shift) with their own dynamic kind, presets and LED colour, so one set can be eight fixed actions while the other is an FX bank; keyboard-macro actions that fire arbitrary key-chords into REAPER; Learn mode and right-click Copy / Paste in the Bindings editor.
- **Generic FX Learn** for any VST/JS/AU plugin parameter, name-substring matched so FX-slot reorders don't break mappings. A plug-in you mapped for the UC1 is already mapped on the UF1: leave the map's UF1 layer switched off and the UF1 fills itself from the UC1 assignments, and switch it on only when the two should differ. Per-mapping knob feel (invert / range / curve / sensitivity / polarity / push-default) can be copied across an entire plug-in map in one action, from the slot right-click menu or the Learn HUD.
- **Sticky Pot**: pin one plug-in parameter per track onto its V-Pot; the pin moves with the track across banks and overrides the strip's normal pan / param view until cleared. Bind *Get next touched Parameter* (arm, then touch the parameter, and it pins to the track it lives on) and *Toggle active/inactive*; a V-Pot press resets the pinned parameter (or clears the pin while arming). Pins are saved with the project.
- **REC + RME (TotalReaper) integration**: per-strip preamp mirror (gain / phantom / pad / phase) plus Shift+V-Pot input-channel switch when a [TotalReaper](https://github.com/acklin83/TotalReaper) instance is detected. On the UF1 (v0.5.4) REC is a selection mode of its own: SEL arms the channel on the fader, and the V-Pot above that fader carries gain and, with Shift, the input channel, with CUT and SOLO assignable to phantom, pad, phase or AutoLevel.
- **Channel-Strip / Bus-Comp favourites**: store several favourite CS / BC plug-in configurations and switch between them from the surface, carrying parameter values across differing plug-ins (grid-scanned and unit-aware) so a switch preserves the sound rather than resetting it.
- **Stream Deck Companion** (optional): a companion plug-in for the Elgato Stream Deck that talks to a TCP bridge inside the extension. Surfaces transport, selection-mode and built-in-action tiles plus a per-track metering tile (peak / GR / combined, with the track's name and colour, an adjustable readout font size, hardware-matching track-name abbreviation that can be switched off for the full name, and a name that can sit at the top of the key and wrap onto a second line, because the bottom of a key is unreadable on an angled deck). Shipped as a ready-to-install `.streamDeckPlugin` on the [Releases page](https://github.com/acklin83/Rea-Sixty/releases/latest); see [`streamdeck/README.md`](streamdeck/README.md).
- **Bitfocus Companion module** (optional, community-supported): the same bridge also drives a native [Bitfocus Companion](https://bitfocus.io/companion) module (actions, variables, feedbacks, a graphical meter bar, presets) so you can control Rea-Sixty from Companion without the Elgato app. The bridge is loopback-only by default but can be opened to the LAN (ExtState `rea_sixty/sd_bridge_bind = lan`) so Companion on another machine can reach REAPER. Source in [`companion/`](companion/).
- **Mapping Exchange**: share and install plug-in mappings (UC1 / UF8 / UC1+UF8) from inside REAPER (the Exchange tab in the Settings window) or on the web at [reasixty.com](https://reasixty.com). Browse by plug-in with a coverage figure and a "works for me" vote, read each mapping as a control-to-parameter table (or compare two side by side), and publish your own with one click from the FX-Learn Share dialog. Sign in with a passkey or a magic link; everything is CC0; mappings only, never `.rea60config` setup bundles.
- **Themed Settings window** rendered through [ReaImGui](https://github.com/cfillion/reaimgui) (REAPER's own ImGui binding, installed alongside Rea-Sixty via ReaPack), docked or floating. Colours are pulled from REAPER's active theme at runtime. Settings for a surface this computer has never had attached are hidden (v0.5.4), search included, so a one-surface rig is not asked to scroll past the other two; unplugging a surface changes nothing, since the filter remembers rather than checks. The full user manual is baked into the build and readable in the **Manual** tab, so it never lags the installed version. A Plug-in Mixer view alongside the Settings tabs is on the roadmap (Phase 2.6) but not yet in this build.

## Why this exists

SSL's UF8 scribble strips can display track colors, but **only** in SSL 360°'s Plugin-Mixer layer, **only** with an SSL plugin loaded on every track, which is unworkable for 100+ track sessions. SSL 360° also holds the UF8 vendor-USB interface with an exclusive claim, so coexistence isn't an option. Either replace SSL 360°, or live with the limitation. Rea-Sixty replaces it.

The protocols, capture workflow, and decisions are documented under `docs/`. Start with `docs/protocol-notes.md`, `docs/protocol-notes-uc1.md`, and `docs/architecture-decision.md`. For user-facing terminology (e.g. **FX vs. Instance**, where the V-Pot FX-Cycle walks all FX on a track, the Encoder Instance Cycle walks only surface-mapped CS / BC / UF8 plug-ins including combos and 360° Link), see [`docs/concepts.md`](docs/concepts.md).

## Repo layout

```
docs/                 Living protocol notes, capture workflows, architecture, plans, legal
captures/             Reference .pcap captures (most gitignored, large binary blobs)
analysis/             Python parsers / diffing tools for USB captures (pyshark)
extension/            The C++ REAPER extension
  src/                Protocol, Palette, UF8Device, UC1Device, ColorSync, Bindings,
                      MixerWindow, MixerLayout, ThemeBridge, SettingsScreen,
                      PluginMap, FocusedParam, HidDevice, …
  tools/              Standalone CLI probes (libusb-only, no REAPER): color test, palette
                      probe, HID probe
  tests/              Pure-logic unit tests (frame bytes, checksum, palette, CSI import)
  vendor/             reaper_imgui_functions.h (the ReaImGui API binding
                      header, so we call ReaImGui via REAPER's GetFunc; the
                      actual ImGui implementation lives in the user's
                      ReaPack-installed reaper_imgui*.so/.dll/.dylib)
  CMakeLists.txt      FetchContent for reaper-sdk + WDL, pkg-config for libusb
```

## Build from source (macOS)

```bash
brew install libusb hidapi cmake pkg-config
cd extension
cmake -B build
cmake --build build -j
```

Default target produces:
- `build/reaper_rea-sixty.dylib`: the REAPER extension (renamed from the
  CMake target `reaper_uf8` for historical / build-script stability)
- `build/libusb-1.0.0.dylib`, `build/libhidapi.0.dylib`: bundled
  runtime deps with rewritten install names (`@loader_path/...`)

Extra targets (build explicitly with `cmake --build build --target <name>`):
- `test_protocol`, `test_uc1_protocol`: unit-test runners
- `uf8_color_test`, `uf8_palette_probe`, `uf8_hid_probe`: standalone CLI
  probes (no REAPER needed)

Pre-built releases for Mac (Apple Silicon, Developer-ID signed + Apple-notarised), Windows (x64) and Linux (x86_64) are on the [Releases page](https://github.com/acklin83/Rea-Sixty/releases). The recommended install path is ReaPack, see the link block at the top of this README. Per-platform manual install docs: [`docs/install-macos.md`](docs/install-macos.md), [`docs/install-windows.md`](docs/install-windows.md), and the `INSTALL.txt` inside the Linux tarball. User-facing reference (Selection Modes, Bindings, Plug-in Mixer Mode, REC + RME, all surface controls): [`docs/user-manual.md`](docs/user-manual.md).

## Contributing

If you own a UF8 or UC1 and want to help, whether that is capture work for layer-switch edge cases, the foot-switch USB events, or extra plugins for the Bus-Comp / Channel-Strip parameter map, open an issue. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the capture-and-decode workflow.

## Legal & Safety

### Trademarks
Not affiliated with, endorsed by, or sponsored by Solid State Logic Ltd. "SSL", "Solid State Logic", "SSL 360°", "UF8", and "UC1" are trademarks of Solid State Logic and are used here solely to identify the hardware and software this project interoperates with (nominative fair use).

### SSL's position on this project
SSL's ACP Support replied to an outreach email from the project author on 2026-05-18:

> *"As there is no affiliation/association to SSL, we have no objections to the public, open source-project. Sadly we're unable to share any protocol documentation [...] Thank you for reaching out and we look forward to seeing where the Rea-Sixty project goes."*

So: no vendor partnership and no protocol disclosure, but no objection to the project existing. Firmware updates that change the wire format are on us to re-decode. See [`docs/capture-workflow.md`](docs/capture-workflow.md) and [`docs/windows-capture-workflow.md`](docs/windows-capture-workflow.md).

### Interoperability basis
Developed via independent, passive observation of the USB wire protocol between legally purchased SSL UF8 / UC1 hardware and legally licensed SSL 360° software, for the sole purpose of achieving interoperability with REAPER. No SSL code, firmware, binaries, or proprietary creative content is decompiled, reproduced, or redistributed.

Legal footing: EU Software Directive [2009/24/EC](https://eur-lex.europa.eu/eli/dir/2009/24/oj) Art. 6 (interoperability exception); §69e UrhG (Germany); 17 USC §1201(f) (US interoperability exception). Rationale recorded in [`docs/interop-rationale.md`](docs/interop-rationale.md).

### No warranty, use at your own risk
This software is provided "as is" with no warranty of any kind (see [`LICENSE`](LICENSE)). It sends vendor-USB frames to the UF8, UC1 and UF1 that are **not** part of SSL's documented public API. Hardware behaviour under unforeseen frames is not guaranteed and has not been exhaustively tested.

**Running third-party firmware-level communication with SSL hardware may void your hardware warranty with Solid State Logic.** If warranty preservation matters to you, do not run this extension.

### License
MIT. See [`LICENSE`](LICENSE).
