# Rea-Sixty

*REAPER × SSL UF8 / UC1 — a native, open-source replacement for SSL 360° on REAPER.*

> [Released](https://github.com/acklin83/Rea-Sixty/releases) — install via ReaPack:
> ```
> https://raw.githubusercontent.com/acklin83/Rea-Sixty/main/index.xml
> ```
> Or download the manual installer for [Mac](https://github.com/acklin83/Rea-Sixty/releases/latest), [Windows](https://github.com/acklin83/Rea-Sixty/releases/latest), [Linux](https://github.com/acklin83/Rea-Sixty/releases/latest).

Rea-Sixty drives the SSL **UF8** and **UC1** controllers directly from a REAPER extension over their vendor-USB protocols — no SSL 360° required, no plugin-on-every-track restriction, no CSI, no virtual MCU MIDI. Track colors land on the UF8 scribble strips in the DAW layer (something SSL 360° does not offer at all), faders run at full 16-bit precision, and the SSL Bus Compressor's GR meter on the UC1 follows audio-driven gain reduction on the focused REAPER track.

## Status

Public beta — Mac (Apple Silicon), Windows (x64) and Linux (x86_64) all ship in [v0.1.1](https://github.com/acklin83/Rea-Sixty/releases/latest). macOS is the primary daily-driver. Phase 1 (UF8 standalone), Phase 2 (UC1 integration with parameter mirror + GR pipeline), Phase 2.5 (Folder Mode, Send/Receive, generic FX Learn — Selection-Set storage still queued), Phase 2.7 (in-app Settings — Device, Bindings, Modes incl. REC + RME (TotalReaper), FX Learn, Selection Sets, Parameter Groups, About) and Phase 2.8 (Nav Mode for Markers + Regions) are shipped. Phase 2.6 (on-screen Plug-in Mixer view) is still pending; the docked window currently hosts the Settings tabs only. Roadmap details in [`ROADMAP.md`](ROADMAP.md); full feature reference in [`docs/user-manual.md`](docs/user-manual.md).

## What it does today

- **Direct REAPER ↔ UF8 / UC1** via `csurf_inst` and libusb. No CSI, no virtual MIDI, no MCU.
- **DAW-layer track colors** on the UF8 scribble strips, polled from `GetTrackColor()` and pushed on bank shifts.
- **Full vendor-USB host responsibility**: init-sequence replay on open, scribble text + value zones, color bars, button/V-Pot/fader/fader-touch routing, LED feedback (solo / mute / select / arm / transport / automation), meter bands, layer management, heartbeat.
- **UC1 parameter mirror** — physical knobs follow the focused track's SSL Bus Compressor / Channel Strip in real time, with values mirrored back to the UC1 displays.
- **UC1 GR display driven by the SSL plugin itself** via `TrackFX_GetNamedConfigParm("GainReduction_dB", …)` — the PreSonus VST3 GR extension that REAPER exposes for the SSL Bus Compressor / Channel Strip Comp + Gate. No JSFX probe, no sidechain tap.
- **Selection Modes** — DAW, Send, Pan, Plug-in Mixer, Folder Mode, Show-Only-Selected; switched from the surface or from the Settings window.
- **Bindings system** — per-strip, transport, global, soft-keys per layer; modifier combos (incl. double-click latch on Shift); 12 user-defined Soft-Key Banks (8 buttons each) plus the 6 SSL stock banks; Learn mode and right-click Copy / Paste in the Bindings editor.
- **Generic FX Learn** for any VST/JS/AU plugin parameter, name-substring matched so FX-slot reorders don't break mappings.
- **REC + RME (TotalReaper) integration** — per-strip preamp mirror (gain / phantom / pad / phase) plus Shift+V-Pot input-channel switch when a [TotalReaper](https://github.com/acklin83/TotalReaper) instance is detected.
- **Themed Settings window** rendered through [ReaImGui](https://github.com/cfillion/reaimgui) (REAPER's own ImGui binding, installed alongside Rea-Sixty via ReaPack), docked or floating. Colours are pulled from REAPER's active theme at runtime. A Plug-in Mixer view alongside the Settings tabs is on the roadmap (Phase 2.6) but not yet in this build.

## Why this exists

SSL's UF8 scribble strips can display track colors, but **only** in SSL 360°'s Plugin-Mixer layer, **only** with an SSL plugin loaded on every track — unworkable for 100+ track sessions. SSL 360° also holds the UF8 vendor-USB interface with an exclusive claim, so coexistence isn't an option. Either replace SSL 360°, or live with the limitation. Rea-Sixty replaces it.

The protocols, capture workflow, and decisions are documented under `docs/` — start with `docs/protocol-notes.md`, `docs/protocol-notes-uc1.md`, and `docs/architecture-decision.md`. For user-facing terminology (e.g. **FX vs. Instance** — the V-Pot FX-Cycle walks all FX on a track, the Encoder Instance Cycle walks only surface-mapped CS / BC / UF8 plug-ins including combos and 360° Link), see [`docs/concepts.md`](docs/concepts.md).

## Repo layout

```
docs/                 Living protocol notes, capture workflows, architecture, plans, legal
captures/             Reference .pcap captures (most gitignored — large binary blobs)
analysis/             Python parsers / diffing tools for USB captures (pyshark)
extension/            The C++ REAPER extension
  src/                Protocol, Palette, UF8Device, UC1Device, ColorSync, Bindings,
                      MixerWindow, MixerLayout, ThemeBridge, SettingsScreen,
                      PluginMap, FocusedParam, HidDevice, …
  tools/              Standalone CLI probes (libusb-only, no REAPER): color test, palette
                      probe, HID probe
  tests/              Pure-logic unit tests (frame bytes, checksum, palette, CSI import)
  vendor/             reaper_imgui_functions.h (the ReaImGui API binding
                      header — we call ReaImGui via REAPER's GetFunc; the
                      actual ImGui implementation lives in the user's
                      ReaPack-installed reaper_imgui*.so/.dll/.dylib)
  CMakeLists.txt      FetchContent for reaper-sdk + WDL, pkg-config for libusb
ROADMAP.md            Phase plan
```

## Build from source (macOS)

```bash
brew install libusb hidapi cmake pkg-config
cd extension
cmake -B build
cmake --build build -j
```

Default target produces:
- `build/reaper_rea-sixty.dylib` — the REAPER extension (renamed from the
  CMake target `reaper_uf8` for historical / build-script stability)
- `build/libusb-1.0.0.dylib`, `build/libhidapi.0.dylib` — bundled
  runtime deps with rewritten install names (`@loader_path/...`)

Extra targets (build explicitly with `cmake --build build --target <name>`):
- `test_protocol`, `test_uc1_protocol` — unit-test runners
- `uf8_color_test`, `uf8_palette_probe`, `uf8_hid_probe` — standalone CLI
  probes (no REAPER needed)

Pre-built releases for Mac (Apple Silicon, Developer-ID signed + Apple-notarised), Windows (x64) and Linux (x86_64) are on the [Releases page](https://github.com/acklin83/Rea-Sixty/releases). The recommended install path is ReaPack — see the link block at the top of this README. Per-platform manual install docs: [`docs/install-macos.md`](docs/install-macos.md), [`docs/install-windows.md`](docs/install-windows.md), and the `INSTALL.txt` inside the Linux tarball. User-facing reference (Selection Modes, Bindings, Plug-in Mixer Mode, REC + RME, all surface controls): [`docs/user-manual.md`](docs/user-manual.md).

## Contributing

If you own a UF8 or UC1 and want to help — capture work for layer-switch edge cases, the foot-switch USB events, or extra plugins for the Bus-Comp / Channel-Strip parameter map — open an issue. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the capture-and-decode workflow.

## Legal & Safety

### Trademarks
Not affiliated with, endorsed by, or sponsored by Solid State Logic Ltd. "SSL", "Solid State Logic", "SSL 360°", "UF8", and "UC1" are trademarks of Solid State Logic and are used here solely to identify the hardware and software this project interoperates with (nominative fair use).

### SSL's position on this project
SSL's ACP Support replied to an outreach email from the project author on 2026-05-18:

> *"As there is no affiliation/association to SSL, we have no objections to the public, open source-project. Sadly we're unable to share any protocol documentation [...] Thank you for reaching out and we look forward to seeing where the Rea-Sixty project goes."*

So: no vendor partnership and no protocol disclosure, but no objection to the project existing. Firmware updates that change the wire format are on us to re-decode — see [`docs/capture-workflow.md`](docs/capture-workflow.md) and [`docs/windows-capture-workflow.md`](docs/windows-capture-workflow.md).

### Interoperability basis
Developed via independent, passive observation of the USB wire protocol between legally purchased SSL UF8 / UC1 hardware and legally licensed SSL 360° software, for the sole purpose of achieving interoperability with REAPER. No SSL code, firmware, binaries, or proprietary creative content is decompiled, reproduced, or redistributed.

Legal footing: EU Software Directive [2009/24/EC](https://eur-lex.europa.eu/eli/dir/2009/24/oj) Art. 6 (interoperability exception); §69e UrhG (Germany); 17 USC §1201(f) (US interoperability exception). Rationale recorded in [`docs/interop-rationale.md`](docs/interop-rationale.md).

### No warranty, use at your own risk
This software is provided "as is" with no warranty of any kind (see [`LICENSE`](LICENSE)). It sends vendor-USB frames to the UF8 and UC1 that are **not** part of SSL's documented public API. Hardware behaviour under unforeseen frames is not guaranteed and has not been exhaustively tested.

**Running third-party firmware-level communication with SSL hardware may void your hardware warranty with Solid State Logic.** If warranty preservation matters to you, do not run this extension.

### License
MIT — see [`LICENSE`](LICENSE).
