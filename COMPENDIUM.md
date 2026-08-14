# Rea-Sixty Compendium

Navigation map of the codebase as it actually is, not as it was planned.
Written so the next session opens the two or three files it needs instead of
reading 111k lines of C++.

Scope: `extension/` (the REAPER extension), `server/` (the mapping exchange
API), `companion/` + `streamdeck/` (optional control-surface clients).
State as of `af96432` (v0.5.2), 2026-08-14.

**Every claim below was read out of the source in one pass.** Where the source
comment and the source code disagree, the code wins and the contradiction is
listed under [Discrepancies found](#discrepancies-found).

---

## 1. Architecture overview

Rea-Sixty is a single REAPER extension DLL (`reaper_rea-sixty.dylib/.dll/.so`)
that talks to three SSL controllers over vendor USB via libusb, replacing SSL
360. It registers as a `csurf_inst` control surface plus a large set of REAPER
actions, and renders its own settings UI through ReaImGui.

```
                       REAPER host process
  +---------------------------------------------------------------+
  |                                                               |
  |  ReaSixtySurface : IReaperControlSurface   (main.cpp:754)     |
  |    Run()      : onTimer() ~30 Hz, the ONE main-thread tick    |
  |    SetSurface*: solo / mute / select / recarm callbacks       |
  |    Extended() : CSURF_EXT_SETFXPARAM broadcast hook           |
  |                                                               |
  |  +---------------------------+  +--------------------------+  |
  |  | onTimerBody_()            |  | MixerWindow (ReaImGui)   |  |
  |  |  main.cpp:32608           |  |  dockable settings win   |  |
  |  |  drainInputQueue()        |  |  12-entry left rail      |  |
  |  |  push* (LED/LCD/meter)    |  |  MixerWindow.cpp:79      |  |
  |  +---------------------------+  +--------------------------+  |
  +-------|--------------|--------------|-------------------------+
          |              |              |
   queueInput()   PendingInput q   ExtState / JSON config
          |              |
  ========|==============|=========== thread boundary ============
          |              |
  +-------+------+ +-----+------+ +------------+  each: own libusb
  | UF8Device    | | UC1Device  | | UF1Device  |  worker thread,
  | PID 0x0021   | | PID 0x0023 | | PID 0x0025 |  send queue + async
  | +ColorSync   | | +UC1Surface| |            |  bulk IN callback
  +--------------+ +------------+ +------------+
          |              |              |
       EP 0x02 OUT / EP 0x81 IN, VID 0x31E9 for all three
```

Side channels, all owned by main.cpp and ticked from `onTimerBody_`:

| Module | Role |
| --- | --- |
| `SslCoreImpersonator` | Stands in for SSL 360 Core so SSL plug-ins stream meter/GR protobuf to us (TCP control + UDP data). Own worker thread. |
| `StreamDeckBridge` | Loopback NDJSON TCP server for the Stream Deck plugin and the Bitfocus Companion module. Own worker thread. |
| `DynaMountManager` | Robotic mic stands over HTTP on the LAN. Own worker thread. |
| `HttpClient` | Async HTTPS for the mapping exchange. One thread per request, platform-native backend. |
| `MidiBridge` | Legacy MCU virtual MIDI. **macOS only**, no-op stubs elsewhere (`MidiBridge.cpp:162`). |

Data model layers, roughly bottom to top:

1. **Wire** `Protocol.{h,cpp}` (UF8), `UC1Protocol.{h,cpp}`, `UF1Protocol.{h,cpp}`.
   Pure byte builders and parsers, no REAPER, unit-testable.
2. **Transport** `UF8Device`, `UC1Device`, `UF1Device`, all the same shape:
   open/claim, replay an init blob, run a worker that drains a send queue and
   emits keepalives, async bulk IN into a parse callback.
3. **Parameter mapping** `PluginMap` (compiled-in SSL maps), `UserPluginCatalog`
   (user FX-Learn maps, JSON), `UC1PluginMap` (UC1 control-id to VST3 tables),
   `AutoLearnEngine` (name-pattern suggestions).
4. **Focus / routing** `FocusedParam` (one global focused parameter shared by
   all three surfaces), `Bindings` (button to action matrix, 3 layers).
5. **Glue** `main.cpp` for UF8 + UF1, `UC1Surface` for UC1.
6. **UI** `MixerWindow` (host + rail), `SettingsScreen*`, `ExchangeView`,
   `ManualView`, `ThemeBridge`.

`main.cpp` is 44k lines and is the integration point for nearly everything:
input drain, all UF1 rendering, all builtins, and roughly 400 `reasixty_*`
C-linkage accessors that `SettingsScreen.cpp` calls. The two files are
deliberately decoupled by that flat function surface rather than by headers.

---

## 2. USB protocol

All three devices share **VID `0x31E9`**, interface 0, **EP `0x02` OUT** and
**EP `0x81` IN**, bulk.

| Device | PID | Declared at |
| --- | --- | --- |
| UF8 (vendor control) | `0x0021` | `UF8Device.cpp:23` |
| UF8 HID (input) | `0x0022` | `HidDevice.h:3` (**unused**, see Discrepancies) |
| UC1 | `0x0023` | `UC1Protocol.h:29` |
| UF1 | `0x0025` | `UF1Device.cpp:21` |

### 2.1 Frame formats (three different dialects)

**UF8** (`Protocol.h:5`)
```
FF <payload...> <ck>          ck = sum(payload) & 0xFF
```
Checksum excludes the leading `FF`. Verify with `uf8::verifyFrame`
(`Protocol.cpp`), compute with `uf8::checksum`.

**UC1** (`UC1Protocol.h:5`)
```
FF <cmd> <len> <payload x len> <ck>    ck = sum(cmd + len + payload) & 0xFF
```

**UF1** (`UF1Protocol.h:11`)
```
FF <op> <len> <len payload bytes> <ck>     total = len + 4
ck = (sum(from the FF opcode through the last payload byte) + 1) & 0xFF
```
Note the `+1` **and** that the `FF` is included. None of the UF8 helpers are
reusable on the UF1.

### 2.2 UF8 command families

Built in `Protocol.cpp`, declared with per-command capture provenance in
`Protocol.h`.

| Opcode | Meaning | Builder |
| --- | --- | --- |
| `FF 66 09 18` | 8 strip colour-bar palette indices | `buildColorCommand` |
| `FF 66 <n> 0B <strip>` | scribble upper row (track name, 7 ch) | `buildStripTextUpper` |
| `FF 66 09 0E <strip>` | scribble lower row (7 ch) | `buildStripTextLower` |
| `FF 66 15 0E <strip>` | value line, 19 ch name+value | `buildValueLine` |
| `FF 66 0A 0C <strip>` | fader dB readout + 2-byte unit | `buildFaderDbReadout` |
| `FF 66 <n> 14 <strip>` | channel-number digits | `buildChannelNumber` |
| `FF 66 06 17 <strip>` | channel-strip type, 4 ch | `buildChannelStripType` |
| `FF 66 <n> 04 <strip>` | plug-in slot name (gates the colour bar) | `buildPluginSlotName` |
| `FF 66 03 06 <lo><hi>` | selected-strip 16-bit bitmask | `buildSelectedStripMask` |
| `FF 66 11 0F <16 B>` | V-Pot readout bars, 2 bytes per strip | `buildVPotReadoutBar` |
| `FF 66 11 0F ...` | layer switch (PM vs DAW) | `buildLayerPluginMixer` / `buildLayerDaw` |
| `FF 1E 03 <strip><lsb><msb>` | motor fader target, 14-bit MCU pitch-bend | `buildFaderPosition` |
| `FF 1D 02 <strip><en>` | fader motor engage / limp | `buildMotorEnable` |
| `FF 38 04` + `FF 39 04` | **coloured LED pair**, must be two separate USB transfers | `buildLedColourPair`, `buildUf8GlobalLed`, `buildTopSoftKeyLed` |
| `FF 3B 03 <id> 00 <st>` | legacy mono LED, needed alongside the pair for some globals | `buildLedCommand` |
| `FF 2D 08` / `FF 4F 02` | LED master brightness / LCD backlight | `buildLedBrightness` / `buildLcdBrightness` |
| `FF 66 21 09` / `0A` | 64-byte keepalive pair, 50 Hz | inline `hb1`/`hb2`, `UF8Device.cpp:300` |
| `FF 66 09 15` / `16` | Comp GR row / Gate GR row, 8 per-strip bytes | stamped into `hb3`/`hb4`, `UF8Device.cpp:404` |

LED cell arithmetic, all capture-derived:
- per-strip Solo/Cut/Sel: `cell = 0x17 - 3*strip - offset`, offset Solo 0, Cut 1, Sel 2 (`Protocol.h:123`)
- per-strip top soft key: `cell = 0x1F - strip` (`Protocol.h:211`)
- global buttons: cells `0x18..0x60`, table in `Protocol.cpp` behind `buildUf8GlobalLed`

UF8 **input** parsing lives in `main.cpp:19294` `onUf8Input`, not in
`Protocol.cpp`. Frame sizes are switched on the command byte at
`main.cpp:19353`:

| cmd | size | meaning |
| --- | --- | --- |
| `0x04` | 6 | poll |
| `0x20` | 6 | capacitive fader touch |
| `0x21` | 7 | fader position |
| `0x22` | 7 | button |
| `0x23` | 6 | pressure sensor (semantics unknown) |
| `0x24` | 6 | V-Pot rotation |
| `0x33` | 6 | pressure sensor (semantics unknown) |

Device button ids map to `bindings::ButtonId` in `Bindings.cpp`
`fromUf8DeviceId`: footswitches `0x00/0x01`, top soft keys `0x18..0x1F`, layer
`0x40..0x42`, quick `0x43..0x45`, send/plugin row `0x48..0x4F`, page
`0x52/0x53`, auto row `0x58..0x5D`, bank-select row `0x68..0x6D`,
selection-mode row `0x70..0x72`, encoder-mode row `0x73..0x76`, bank
`0x78/0x79`, zoom `0x7A..0x7E`.

### 2.3 UC1 command families

`UC1Protocol.h` carries a fully annotated command list. Highlights:

- `FF 1B 01 <ctr>` keepalive, counter cycles 0..3.
- `FF 5B 02 <hi><lo>` GR meter, 16-bit big-endian in tenths of a dB.
  **This stream is the UC1's liveness watchdog**, streamed at 50 Hz from
  `UC1Device::workerLoop_`; stopping it makes the UC1 fall back to
  "Attempting to reconnect to SSL 360".
- `FF 13 04 <bank> <cell> 01 <state>` the universal LED/VU cell write.
  `state` 0x00 off, 0x33 dim (bypassed), 0xFF on.
- `FF 66 <len> <zone> <ascii>` display zones. Zone map at `UC1Protocol.h:164`:
  `0x02` CS context (37 B), `0x03` CS readout (22 B), `0x04` global status
  (43 B), `0x05` BC readout (22 B), `0x0E` state tag, `0x10` plug-in name tag.
- `FF 66 03 00 <mode> 00` central-panel mode banner. `CentralMode` enum at
  `UC1Protocol.h:398`: Main / PresetsSub / Presets / Routing / Transport /
  ExtFuncs.
- Seven-segment display: per-segment LED cells, ones `0x10..0x16`, tens
  `0x08..0x0E`, hundreds only partly decoded (`UC1Protocol.h:252`).

Knob and button ids are named constants in `uc1::knob` and `uc1::button`
(`UC1Protocol.h:38` and `:92`). Note the collision: id `0x0D` is the CHANNEL
encoder as a knob event (`FF 24`) and the secondary-encoder push as a button
event (`FF 22`); the event family disambiguates.

### 2.4 UF1 command families

`UF1Protocol.h`. The IN stream carries a 2-byte report header `32 <xx>`
(`0x60` = event, `0x00` = idle) followed by concatenated `FF` frames;
`uf1::parseInputStream` walks all of them and returns the consumed byte count
so `UF1Device` can keep a cross-URB residual (`UF1Device.h:96`).

| Opcode | Meaning |
| --- | --- |
| `FF 20 02 00 <st>` | fader touch |
| `FF 21 03 00 <lo><hi>` | fader position, **15-bit** 0..0x7FFF (not 14-bit like the UF8) |
| `FF 22 03 <id> 00 <st>` | button |
| `FF 23 02 <id> <st>` | encoder touch |
| `FF 24 02 <id> <delta>` | encoder rotate, 6-bit signed |
| `FF 38 04 <id> 00 <XX><YY>` | colour: `XX = (g4<<4)|r4`, `YY = 0xF0|b4` |
| `FF 39 04 <id> 00 <lvl> F0` | LED level, needs the `FF 38` companion to be visible |
| `FF 3B 03 <id> 00 <st>` | mono LED |
| `FF 1D 02 00 <en>` / `FF 1E 03 00 <lo><hi>` | motor limp / target |
| `FF 67 <len> <addrHi><addrLo> <payload>` | **screen element write** |
| `FF 1B 01 <ctr>` | keepalive, 150 ms, counter 0..3 |

Screen element addresses are in `uf1::scr` (`UF1Protocol.h:172`). Channel zone
is `0x00xx` (track name `0x000b`, output dB `0x000c`, pan `0x000e/0x000f`,
channel number `0x0014`, CS type `0x0017`, colour bar `0x0018`, small-LCD soft
key label `0x0004` and its backdrop `0x0000`). Large LCD is `0x01xx`
(soft-key labels `0x0104`, focused param `0x010e`, V-Pot bars `0x010f`,
timecode `0x0119`, solo-active flag `0x0120`, header row `0x011c`, graphic
`0x0122`).

Two hard-won facts are recorded at `UF1Protocol.h:216` and are worth repeating:

- `0x011b` is a **command** element. It must be written with a zero-length
  payload the way SSL does it. Writing bytes to it tears down the whole channel
  layout and needs a REAPER restart.
- `0x011d` is **not** a view selector. Channel vs Meter view is toggled by the
  MODE button on the device itself; the host never commands it. Do not
  reintroduce a `kViewMode`.

Init blobs are captured SSL 360 boot sequences, replayed verbatim on open:

| File | Lines | Source capture |
| --- | --- | --- |
| `init_sequence.inc` | 1535 | UF8 cold start, `cap_init_2026-05-07.pcapng` |
| `layer_plugin_mixer.inc` | 759 | UF8 layer-switch state flood, `cap13` |
| `uc1_init_sequence.inc` | 2799 | UC1 LED init flood, `uc1_23_ssl360_startup` |
| `uf1_init_sequence.inc` | 643 | UF1 cold start, `cap101` |

Frames carry `delay_ms_before`; the gaps are load-bearing (fader self-test
needs real time to move).

### 2.5 SSL plug-in meter protocol (not USB)

`SslMeterProtocol.{h,cpp}` decodes `PluginMeterDataMessage`, plain unencrypted
protobuf over UDP. `SslCoreImpersonator.{h,cpp}` does the socket side:
announce our TCP port on discovery ports 16008/16009, accept the plug-in's TCP
connection, send the Core handshake assigning a UDP data port (default 16010),
then receive meter frames.

Frame: `ef bc 51 00` magic, `uint32` length, 28-byte SSL header, protobuf.
Silence sentinel float is `0xffc00000`.

Two things that cost days and are documented in the header:
- `sslcore::setView()` is **not cosmetic**. The plug-in only computes the
  meters its selected view needs. View 0 streams the Lissajous and not the RTA;
  view 2 is the reverse (`SslCoreImpersonator.h:36`).
- Only `VuPpm(0)` ever carries overload flags, and not while the Overview view
  is selected (`SslCoreImpersonator.h:60`).

---

## 3. Central data flows

### 3.1 Fader movement, UF8 hardware to REAPER

```
libusb IN callback (worker thread)
  UF8Device::readCallback_            UF8Device.cpp:517
    rawInputHandler_                  wired at main.cpp:18832
      onUf8Input(data, len)           main.cpp:19294
        stitch g_inputResidual, walk FF frames
        cmd 0x21 "FF 21 03 <strip><A><B>"   main.cpp:19400
          gate 1: only while g_touchReported[strip]   (kills motor echo)
          gate 2: only frames with bit 7 of <A> set   (the authoritative
                  stream; bit-7-clear frames are a second signal that
                  carries upward spikes)
          touch anchoring: effPb = pb14 - readbackOffset
                  offset learned from (grab - anchor) while parked
          queueInput(PendingInput{VolumeAbs, strip, pbToLinearVolume(effPb)})
  ------------------------------------ thread boundary (mutex queue)
onTimer -> onTimerBody_               main.cpp:32584 / :32608
  drainInputQueue()                   main.cpp:15877
    coalesce per strip, resolve StripRoute (track / send / receive / DynaMount)
    CSurf_OnVolumeChange or writeRouteVolumeLinear_ / writeRouteVolAutomation_
```

Return path, same tick:

```
onTimerBody_
  rebuildVisibleTrackList()           main.cpp:1899
  pushZonesForVisibleSlots()          main.cpp:27587   names, values, colour bars
  computeStripCurrentPb_()            main.cpp:14582   target fader position
  g_dev->send(buildFaderPosition(...))
  pushVuMeter / pushSelColourBar / pushUf8GlobalLeds / pushAutoModeLedsMixed_
  UF8Device worker drains the queue and writes EP 0x02
```

Key invariant: **nothing on the worker thread touches the REAPER API.** Every
handler enqueues a `PendingInput` (the enum at `main.cpp:4384` lists roughly 50
kinds) and the main-thread drain executes it. Violating this is a documented
crash class.

### 3.2 A parameter edit, any source, reaching all three surfaces

```
source: UC1 knob | UF8 V-Pot | UF1 V-Pot | plug-in GUI mouse | automation
   |
   +-- plug-in GUI / automation: ReaSixtySurface::Extended(CSURF_EXT_SETFXPARAM)
   |                                                    main.cpp:18648
   +-- hardware: the surface's own dispatch writes TrackFX_SetParamNormalized
   |
   v
uf8::setFocus(FocusedParam{domain, linkIdx})            FocusedParam.h:80
   sets g_focusedDirty
   v
onTimerBody_ sees the dirty flag
   UF8:  pushZonesForVisibleSlots re-renders every strip's value line
   UC1:  UC1Surface::pushFocusedParamReadout_ writes zone 0x03 or 0x05
   UF1:  uf1PaintChannel_ / uf1PaintEqGraph_ repaint the screen
   HUD:  publishHud_ / publishOverlay* write ExtState for the Lua companions
```

`FocusedParam.slotIdx` is an **SSL 360 Link linkIdx**, not an array index. That
is what lets the same focus render on tracks carrying different plug-in
variants: resolve it per track with `uf8::findSlotByLinkIdx`
(`PluginMap.h`). Getting this wrong shows the wrong parameter name on
a strip whose plug-in orders its slots differently.

### 3.3 Track colour to UF8 scribble strip

```
onTimerBody_
  ColorSync::refresh(getTrackColor lambda)     ColorSync.cpp
    for each of 8 visible slots: GetTrackColor
    uf8::quantize(rgb) -> palette index 0..15  Palette.cpp
    diff against lastPushed_; unchanged -> no USB traffic
    changed -> dev.send(buildColorCommand(indices))
```
`invalidate()` forces the next refresh to push regardless; called on device
open and on bank shift (`g_bankDirty`).

### 3.4 FX Learn, mapping a third-party plug-in

```
Settings -> FX Learn (SettingsScreen.cpp:18310 drawFxLearn)
  or the Learn HUD Lua companion via ExtState commands
    |
    v
AutoLearnEngine::suggestSlots / suggestUf8Banks / suggestUf8Strips
    name-pattern match against built-in SSL names + already-learned maps
    |
    v
UserPluginMap  { match, domain, uf8Mode, uf1Mode,
                 slots[]        -> UC1 controls, by linkIdx
                 uf8.banks[2][8][8] + uf8.strips[2][8]
                 uf1.vpots[] + uf1.softKeys[]   (sparse, by flat pos)
                 metering, extFuncs, paramLabels, paramSnapshot }
    |
    v  user_plugins::save()
<REAPER resource>/rea_sixty/user_plugins.json   (format version 16)
    |
    v  runtime lookup
uf8::lookupPluginMapByName   built-ins first, then user maps
```

The UF1 has a **fallback** path: with no explicit `uf1` block it fills its
V-Pots and soft keys sequentially from the UC1 `slots`, sorted by linkIdx, 4
per page (`uf1LearnedStreamSlots_`, `main.cpp:23593`). An explicit map
overrides that and is deliberately layer-free (no modifier overlays).

### 3.5 Mapping exchange round trip

```
extension                              server (Fastify + SQLite)
  ExchangeView.cpp  ---- HTTPS ---->   routes/browse.js   GET  /v1/plugins
  reasixty::http                       routes/maps.js     GET  /v1/maps/:id
   (macos_http.mm / win_http.cpp        routes/upload.js   POST /v1/maps
    / linux_http.cpp)                   routes/auth.js     passkey + magic link
  .rea60map envelope                    ingest/merge.js    parse + dedupe
  user_plugins::importMapFromString     db/schema.sql      maps, map_bindings,
                                                           uf8_slots, uf1_slots
```
Only single plug-in maps cross the wire, never `.rea60config` setup bundles.
The reason is in `UserPluginCatalog.h:771`: a bundle embeds `bindings.json`,
which can carry REAPER action ids and keyboard macros, so importing one would
run whatever the author put in it. A map is pure data.

---

## 4. Gotchas

Ordered by how expensive they were.

**Threading**
- REAPER's track and FX API is main-thread only. Every USB worker callback must
  go through `queueInput` and `drainInputQueue`. This includes the
  `registerBuiltin` lambdas: they run on the input thread.
- `onTimer` wraps `onTimerBody_` in try/catch (`main.cpp:32584`) because an
  escaping C++ exception is fatal to REAPER's run loop. `Extended` has the same
  wrapper around `extendedBody_` (`main.cpp:18648`). The exception is rethrown
  when it came from inside an ImGui frame, because swallowing it there corrupts
  the ImGui stack.

**Wire**
- The `FF 38` / `FF 39` LED pair must traverse the bus as **two separate USB
  transfers**. Combining them makes the UF8 firmware treat the second as
  garbage and stall subsequent commands, most visibly killing fader-motor
  writes (`Protocol.h:188`).
- UC1: if the `FF 5B` GR stream stops, the device declares itself lost. It is
  the liveness heartbeat, not just a meter.
- UF1 `FF 39` alone is inert for button LEDs; the `FF 38` companion is required
  (`UF1Protocol.h:120`).
- UF1 `0x011b` with a non-empty payload destroys the channel layout. See 2.4.
- Init replay: `initInProgress_` makes the worker skip the user send queue so
  REAPER's timer pushes cannot interleave with init frames. Keepalives and IN
  reads continue throughout.
- All three devices carry a stale-handle detector (`needsReopen()`), set after
  roughly 1 s of consecutive `NO_DEVICE` / `NOT_FOUND` / `IO` on bulk OUT.
  macOS re-enumerates after sleep/wake. The main thread polls and recreates.

**Fader**
- Two distinct position streams arrive on `FF 21 03`. Only frames with bit 7 of
  the LSB byte set are authoritative. Bit-7-clear frames carry +50..+100 LSB
  upward spikes and must be dropped, **skipping the full 7 bytes**; an earlier
  hardcoded 6 slid the parser into the checksum and broke fader 8 specifically
  (`main.cpp:19440`).
- Fader position is only read while `g_touchReported[strip]` is set, otherwise
  REAPER's own motor echo feeds back.
- Touch anchoring exists because the readback sits about 100 LSB away from the
  drive value. Without it, merely touching a parked fader nudges the trim.

**Platform**
- Windows main thread has a 1 MB stack and MSVC probes the whole frame in the
  prologue, so an oversized local crashes in `__chkstk` with no usable trace.
  This has happened twice. The build now fails on any frame over 256 KB via
  `-Wframe-larger-than` (`CMakeLists.txt:430`). MSVC has no equivalent flag, so
  clang and GCC catch it for everyone.
- Do **not** call `SetDefaultDllDirectories` on Windows. It is process-global
  and permanent, and it broke Acustica/Acqua plug-ins that load sub-DLLs from
  their own directory. The entry point instead preloads libusb and hidapi by
  full path (`main.cpp:43726`).
- macOS 15 SIGKILLs on the first invalid code-signature page during
  `dlopen`. `install_name_tool` invalidates the signature, so every bundled
  dylib and our own must be re-signed ad hoc after the rewrite
  (`CMakeLists.txt:591`).
- Linux: libstdc++ is linked **statically** with `--exclude-libs,ALL`. A GCC 13
  host otherwise records `GLIBCXX_3.4.31` and the module will not load on
  Debian 12. The glibc floor is handled separately by building in an
  `ubuntu:22.04` container.
- SWELL and windows.h both define `min`/`max` as macros. `WDL_NO_DEFINE_MINMAX`
  is set project-wide and `NOMINMAX` on Windows.
- `MidiBridge` is CoreMIDI, macOS only. On Windows and Linux every method is a
  stub returning false (`MidiBridge.cpp:162`). The MCU passthrough for
  unhandled UF8 buttons therefore only exists on macOS.

**Data**
- UF8 and UF1 scribble LCDs render **Latin-1**. Fold UTF-8 exactly once, at the
  emit site, with `utf8ToLatin1` (`TrackName.h`). UC1 uses a different encoding
  and ImGui renders UTF-8 natively; folding for either produces mojibake.
- `fxIdentityName` (original_name) is FX **identity**, `TrackFX_GetFXName` is
  **display** and honours user renames. Behaviour lookups must use the former.
- Acustica/Acqua plug-ins fault inside their own code when polled from the
  control-surface thread. `fxIsAcustica` matches the vendor tag, which is
  present in the display name but not in `original_name`, so both are checked
  (`PluginMap.h`).
- JSFX sliders mis-report `pStep` as 1.0 (full range). `jsfxStepClassify`
  normalises the real step and decides continuous vs enum
  (`PluginMap.h`, `UserPluginCatalog.h:247`).
- `kUserUf1MaxPages = 16` is a **hard ceiling**, not a suggestion. Without it a
  "Fill: Replace" on a plug-in with hundreds of parameters draws hundreds of
  page buttons on one `SameLine` row and takes REAPER down. It is clamped at
  the source in `uf1MapPageCount` so old oversized maps survive being opened
  (`UserPluginCatalog.h:524`).
- ReaPack installs every file of an `type="extension"` package into
  `UserPlugins`, which is why the bundled Linux `.so` deps land next to the
  module and `$ORIGIN` resolves them.

**Config locations** (all under the REAPER resource path)
```
rea_sixty/bindings.json          Bindings::configPath()
rea_sixty/user_plugins.json      user_plugins::configPath()
rea_sixty/parameter_groups.json  ParameterGroups.cpp:119
rea_sixty/favourite_sets.txt     main.cpp:3187 favSetsFilePath_
Scripts/rea-sixty/*.lua          deployed from embedded headers on load
Effects/Rea-Sixty/*.jsfx         deployed from embedded headers on load
```
Everything else is ExtState under section `rea_sixty` (and a few legacy keys
under `ReaSixty`). Project-scoped state (selection sets, parameter-group
membership, sticky pins, favourite banks) goes through
`project_config_extension_t` into the RPP, **not** `SetProjExtState`.

---

## 5. Build, test, deploy

### Build

```bash
brew install libusb hidapi cmake pkg-config     # macOS
cd extension
cmake -B build
cmake --build build -j
```

Output is `build/reaper_rea-sixty.dylib` plus re-signed `libusb-1.0.0.dylib`
and `libhidapi.0.dylib` with `@loader_path` install names. The CMake target is
still called `reaper_uf8` for build-script stability; only the output name
carries the product brand.

Windows needs `-DLIBUSB_ROOT=<path> -DHIDAPI_ROOT=<path>` (MSVC release
layouts). Linux uses pkg-config and prefers `hidapi-hidraw` so HID access does
not contend with the libusb claim on the same root hub.

Generated headers, all produced by `cmake/embed_factory_bundle.cmake`:

| Header | Source |
| --- | --- |
| `commit_count.h` | git describe, regenerated on **every** build |
| `factory_bundle.h` | `resources/factory.rea60config` |
| `input_level_jsfx.h` | `jsfx/rea_sixty_input_level.jsfx` |
| `inserts_overlay_lua.h`, `assignment_hud_lua.h`, `focused_panel_lua.h`, `mode_banner_lua.h` | `scripts/*.lua` |
| `user_manual.h` | `../docs/user-manual.md` |
| `winusb_inf.h` | `resources/rea_sixty_winusb.inf` (Windows only) |

`SettingsScreen.cpp` and `ManualView.cpp` carry explicit `OBJECT_DEPENDS` on
their generated headers so a fresh version string or manual actually reaches
the binary.

### Test

CMake registers five ctest targets (non-Windows only):

| Target | Covers |
| --- | --- |
| `test_protocol` | UF8 frame bytes, checksum, palette quantisation |
| `test_uc1_protocol` | UC1 frame construction and parsing, vectors from real captures |
| `test_dynamount` | DynaMount request builders, clamping, response check |
| `test_ssl_meter` | protobuf decode against frames carved from `cap87` |
| `test_user_catalog_uf1` | v11 UF1 map JSON round trip, the silent-data-loss class |

```bash
cmake --build build --target test_protocol && ./build/test_protocol
ctest --test-dir build
```

`tests/cs_transfer_test.cpp` is **not** a CMake target by design; it
re-implements the pure decision logic of `applyParamValue_` and is built by
hand (`clang++ -std=c++17 -O2 -o /tmp/cstest tests/cs_transfer_test.cpp`).

Server tests: `cd server && npm test` (node:test, 12 suites).

Standalone libusb probes, no REAPER needed: `uf8_color_test`,
`uf8_palette_probe`, `uf8_hid_probe`, `ssl_core_probe`.

### CI

`.github/workflows/build.yml` builds four targets on push to main, on tags
`v*`, and on PRs: linux-x86_64 (inside an `ubuntu:22.04` container for the
glibc floor), macos-arm64, macos-15-intel, windows-latest. It is a
**compile check only**; no job runs ctest.

### Deploy

Canonical runbook: `.local-docs/release-process.md`. Shape: tag, let CI build
the Linux and Windows artifacts, build and notarise the two macOS slices
locally (notarisation needs the Keychain secret and cannot run in CI), stage
into `dist/`, publish the GitHub release, then regenerate the ReaPack index in
the separate `acklin83/reaper-scripts` repo.

---

## 6. Open TODOs and known soft spots

**Deliberately unfinished**
- **Phase 2.6, the on-screen Plug-in Mixer.** `MixerLayout.cpp` is a 33-line
  scaffold that draws one line of placeholder text, and `MixerLayout::draw` is
  **never called** from anywhere. It is still compiled into the target. The
  docked window hosts the Settings rail only.
- `SettingsScreen.cpp:4632` marks an entire "User-Quick slot editor (OLD
  section-style)" region as UNUSED. It is still compiled.
- UC1 seven-segment hundreds digit is only partly decoded; values over 99
  render ones and tens correctly and leave the hundreds cells inherited
  (`UC1Protocol.h:252`).
- UF8 `FF 23` and `FF 33` input frames are consumed with the right length but
  their semantics are unknown ("pressure sensor (TBD)").
- Linux modifier-key polling for Alt-drag snap-back is not implemented and
  returns false, so the feature no-ops cleanly there (`main.cpp:23`).
- UC1 Magnifier button has no decoded hardware LED cell; the binding lights the
  on-screen mockup only (`Bindings.h:139`).

**Known-fragile**
- FX-param touch return: a UF8 plug-in-mode fader bound to an FX param does not
  return to the envelope during playback in Touch mode. Consciously parked.
- Fader knob-travel curves were tried and reverted the same day: absolute
  position plus motor feedback creates round-trip races. The V-Pot keeps the
  feature. Any retry needs touch-snapshot echo, not curve math
  (`UserPluginCatalog.h:419`).
- `main.cpp` at 44k lines and `SettingsScreen.cpp` at 21.5k are the two files
  where every change risks a merge conflict and where the frame-size guard
  earns its keep.

**Dead or orphaned code** (candidates for step 2, listed here so the decision
is deliberate)
- `HidDevice.{h,cpp}` plus `g_hid` (`main.cpp:408`) and `logHid`
  (`main.cpp:26585`): compiled and linked, never opened, never called.
- `uf8::buildMeter` (`Protocol.cpp:87`): declared, defined, no call site.
- `uf8::buildPluginMixerHeartbeat`: only referenced from `test_protocol.cpp`.
- `MixerLayout.{h,cpp}`: no call site.
- `jsfx/rea_sixty_gr_probe.jsfx`: not embedded by CMake, not referenced by any
  source. Only `docs/protocol-notes-uc1.md` still describes it as the GR
  mechanism, which the README explicitly contradicts.

---

## 7. Discrepancies found

Comment or doc versus actual code. Input for the step 2 cleanup.

| # | Where | Says | Actually |
| --- | --- | --- | --- |
| 1 | `main.cpp:1` file header | "we simply use CountTracks() and show tracks 1..8 regardless of scroll, bank-shift hookup is a follow-up" | `rebuildVisibleTrackList` (`main.cpp:1899`) does full banking, folder mode, show-only-selected, pinned master, UF1 extender. |
| 2 | `HidDevice.h:3` | describes PID 0x0022 as the UF8 input path, "parsing to MCU happens in main.cpp once we've characterized the report format" | Never opened. UF8 input comes from the libusb bulk IN on PID 0x0021 via `setRawInputHandler`. The whole TU is dead. |
| 3 | `Protocol.h:25` `ButtonEvent::id` | "0x78 = BANK->, 0x79 = BANK<-, others TBD" | The full id table is decoded in `Bindings.cpp` `fromUf8DeviceId`, roughly 50 ids. |
| 4 | `Protocol.h:71` `buildMeter` | "Best-guess from cap10 frame layout ... semantics still partially TBD" | No call site at all. VU goes out via `buildVuMeter`. |
| 5 | `Bindings.h:3` header block | "Phase A ... no UI yet"; "per-strip Sel/Cut/Solo/Rec stay hardcoded"; "per-strip top soft-key (0x18..) also stay hardcoded in v1" | Full Settings UI exists; `TopSoftKey1..8` and `Uf8Select` are first-class `ButtonId`s with factory bindings. |
| 6 | `UserPluginCatalog.h:11` | "Phase 2.5d-A Step 1, data layer + JSON I/O only. No UI, no FX-Learn dispatch yet" | Full FX-Learn UI, HUD, dispatch, exchange upload. |
| 7 | `UserPluginCatalog.h:706` version history | narrates v1 through v8 | `kCurrentFormatVersion = 16`. v9 through v16 are squeezed into the trailing comment on the constant line instead of the block. |
| 8 | `UC1Device.h:4` | "UC1 needs no custom init sequence" | `UC1Device.cpp:19` includes `uc1_init_sequence.inc` and replays it at `UC1Device.cpp:271`. |
| 9 | `MixerWindow.h:12` | "Phase 2.6a scaffold. Bodies are stubs until ImGui has been vendored into extension/vendor/imgui/ and icontheme.h has been pulled from upstream" | Fully implemented, 868 lines, and it uses **ReaImGui via GetFunc**, not a vendored ImGui. `vendor/imgui/` does not exist. |
| 10 | `MixerLayout.h:16` | "Phase 2.6 scaffold; bodies arrive in 2.6b/2.6c" | Accurate about the body, but omits that nothing calls `draw` at all. |
| 11 | `HttpClient.h:12` | "Only macOS is implemented today; the others return a clear error until built" | All three are implemented (`macos_http.mm`, `win_http.cpp`, `linux_http.cpp`) and built by CMake. `HttpClient.cpp` is the never-taken fallback. |
| 12 | `StreamDeckBridge.h:36` | "see tests/test_sdbridge.cpp" | That file does not exist. |
| 13 | `streamdeck/.../plugin.js:6-12` | "WebSocket (Node 22+ global WebSocket)", "manifest.json Nodejs.Version = 24", "No npm dependencies" | `manifest.json` says Version "20", `package.json` depends on `ws`, and lines 19-22 of the same file explain why. Three contradictions in one header. |
| 14 | `scripts/rea_sixty_assignment_hud_imgui.lua:6` and `rea_sixty_focused_panel_imgui.lua:6` | "SPIKE / parallel variant of rea_sixty_assignment_hud.lua" | The non-imgui originals are gone, and `reasixty_cleanupLegacyLua` (`main.cpp:43388`) actively deletes them from the user's Scripts folder. These are the only version. |
| 15 | `docs/protocol-notes-uc1.md:255` | "GR is computed by a bundled JSFX envelope-follower (`rea_sixty_gr_probe.jsfx`)" | README.md line 31 says "No JSFX probe, no sidechain tap." GR comes from `GainReduction_dB` plus the SSL Core impersonator. The JSFX file is orphaned. |
| 16 | `.local-docs/release-process.md` repo table | source repo at `~/Documents/dev/reaper-uf8` | The working copy is `~/Documents/dev/Rea-Sixty`. The old path does not exist. |
| 17 | `ColorSync.h` | declares `refresh(const std::function<...>&)` | Does not include `<functional>`; it compiles only through the transitive include in `Protocol.h`/`UF8Device.h`. |
| 18 | `SettingsScreen.cpp:4632` | "User-Quick slot editor (OLD section-style) UNUSED" | Honest, but the region is still compiled. |
| 19 | `CMakeLists.txt:707` | "tests + CLI helpers" wired with `enable_testing()` | No CI job ever runs `ctest`. The workflow is compile-only. |

Items 1, 2, 5, 6, 8, 9, 11, 12, 13, 14, 15, 16 are plain stale text and are
safe to correct without touching behaviour. Items 4, 10, and the orphans in
section 6 involve deleting code and want a decision first.

Checked and found **accurate**, so no cleanup needed: the `Protocol.h:319` GR
comment (it explicitly corrects the older "single-byte frame" note and points
at `UF8Device::setGrBytes`), and `MixerLayout.h`'s scaffold claim about its own
body.

---

## 8. File index

One line each. Line counts are the current state.

### extension/src, wire protocol

| File | L | Purpose |
| --- | --- | --- |
| `Protocol.h/.cpp` | 362/858 | UF8 frame builders and parsers, LED colour tables, palette anchors. Pure, testable. |
| `UC1Protocol.h/.cpp` | 487/590 | UC1 frame builders, knob/button/LED/zone id constants, central-mode enum. |
| `UF1Protocol.h/.cpp` | 226/167 | UF1 frame builders, input-stream walker, encoder/button ids, screen element addresses. |
| `Palette.h/.cpp` | 35/206 | REAPER RGB to UF8 16-entry palette index, nearest match. |
| `init_sequence.inc` | 1535 | Captured UF8 cold-start replay blob. |
| `layer_plugin_mixer.inc` | 759 | Captured UF8 layer-switch state flood. |
| `uc1_init_sequence.inc` | 2799 | Captured UC1 LED init flood. |
| `uf1_init_sequence.inc` | 643 | Captured UF1 cold-start replay blob. |
| `uf1_loudness_chrome.h` | 165 | Static UF1 loudness-plot chrome sub-frames, per scale range. |

### extension/src, transport

| File | L | Purpose |
| --- | --- | --- |
| `UF8Device.h/.cpp` | 152/569 | libusb wrapper for the UF8: claim, init replay, send queue, 50 Hz heartbeat with GR stamping, async bulk IN. |
| `UC1Device.h/.cpp` | 116/618 | Same for the UC1, plus the 50 Hz `FF 5B` GR liveness stream. |
| `UF1Device.h/.cpp` | 113/506 | Same for the UF1, plus `sendBurst` for atomic image chunks and a cross-URB read residual. |
| `HidDevice.h/.cpp` | 53/55 | hidapi reader for UF8 PID 0x0022. **Currently unused.** |
| `MidiBridge.h/.cpp` | 76/175 | CoreMIDI virtual MCU ports. macOS only; stubs elsewhere. |
| `LogPath.h/.cpp` | 24/27 | Per-platform diagnostic log directory (`/tmp` vs `%TEMP%`). |

### extension/src, parameter model

| File | L | Purpose |
| --- | --- | --- |
| `PluginMap.h/.cpp` | 253/745 | Compiled-in SSL plug-in maps (CS 2, 4K B/E/G, BC 2, 360 Link), linkIdx conventions, FX identity/Acustica/JSFX/SSL-360 predicates. |
| `UserPluginCatalog.h/.cpp` | 914/2145 | User FX-Learn maps: data model, knob-travel math, step helpers, JSON v16 load/save, `.rea60map` share envelope. |
| `UC1PluginMap.h/.cpp` | 276/1386 | UC1 control-id to VST3 param tables per SSL plug-in, plus instance-index state. |
| `AutoLearnEngine.h/.cpp` | 96/940 | Name-pattern auto-mapping suggestions for UC1 slots, UF8 banks and UF8 strips. |
| `FocusedParam.h/.cpp` | 88/20 | The single global focused parameter (domain + linkIdx) shared by all three surfaces. |
| `KnobFeelPresets.h/.cpp` | 50/178 | Ten named, globally persisted knob-feel bundles. |
| `VirtualNotch.h/.cpp` | 35/70 | Magnet-to-centre detent for relative encoder writes, with an optional hold. |
| `GrCalibration.h` | 53 | Piecewise-linear GR correction shared by UC1, UF8 and the FX-Learn preview. |
| `PluginChunkPatch.h/.cpp` | 60/358 | Toggle SSL HQ Mode and A/B by patching the base64 VST3 chunk (no API exists). |

### extension/src, glue and features

| File | L | Purpose |
| --- | --- | --- |
| `main.cpp` | 44251 | Entry point, `ReaSixtySurface`, `onTimer`, all input drain, all UF1 rendering, all builtins, all `reasixty_*` accessors. |
| `UC1Surface.h/.cpp` | 546/5419 | UC1 REAPER glue: event queue, poll, LED rings, display zones, central-panel modes, GR polling. |
| `Bindings.h/.cpp` | 836/5061 | Button-to-action model (3 layers, 4 modifiers, short/long/double, chains), builtin registry, JSON persistence, user Quick banks, UF1 soft-key banks. |
| `ColorSync.h/.cpp` | 45/36 | Diffed push of the 8 visible track colours to the UF8 scribble bars. |
| `TrackName.h/.cpp` | 36/227 | UTF-8 to Latin-1 folding and shared track-name abbreviation. |
| `KeyMacro.h/.cpp` | 75/215 | Chord parsing and in-process key delivery to REAPER's main window. |
| `ParameterGroups.h/.cpp` | 181/583 | 8 persistent groups plus temp-from-selection; fans hardware writes out to member tracks. |
| `MarkerOverlay.h/.cpp` | 190/515 | Nav Mode data layer: regions/markers views, 2-level drill, cursor, paging. |
| `NavDispatch.h/.cpp` | 30/192 | The shared push-gesture action dispatch for Nav Mode. |
| `SetupBundle.h/.cpp` | 55/321 | `.rea60config` export/import of the whole configuration, plus embedded factory restore. |
| `DynaMountClient.h/.cpp` | 111/250 | DynaMount wire protocol, pure builders plus the socket send. |
| `DynaMountManager.h/.cpp` | 153/316 | Up to 8 mounts, config, calibration, and the worker that pushes positions. |
| `StreamDeckBridge.h/.cpp` | 96/362 | Loopback NDJSON TCP server. Zero REAPER dependencies by design. |
| `SslMeterProtocol.h/.cpp` | 121/116 | Pure protobuf decode of `PluginMeterDataMessage`. |
| `SslCoreImpersonator.h/.cpp` | 281/1922 | SSL 360 Core impersonation: discovery announce, TCP handshake, UDP meter intake, per-instance correlation. |
| `HttpClient.h/.cpp` | 45/30 | Async HTTPS interface plus the never-taken fallback stub. |
| `macos_http.mm` | 147 | NSURLSession backend. |
| `win_http.cpp` | 309 | WinHTTP backend. |
| `linux_http.cpp` | 316 | libcurl via `dlopen`, option ids spelled out so no curl headers are needed. |
| `macos_save_dialog.mm` | 71 | NSSavePanel, bypassing SWELL. |
| `macos_open_dialog.mm` | 44 | NSOpenPanel, same reason. |
| `macos_pin_fx_gui.mm` | 164 | AppKit window positioning for the pinned FX GUI. |

### extension/src, UI

| File | L | Purpose |
| --- | --- | --- |
| `MixerWindow.h/.cpp` | 51/868 | ReaImGui host window, dockable, 12-entry left rail with search, font and theme refresh. |
| `SettingsScreen.h` | 65 | The ten `draw*` pane entry points plus the shared search matcher. |
| `SettingsScreen.cpp` | 21573 | Bindings, FX Learn, Modes, Favourites, Selection Sets, Parameter Groups, About, all HUD publishers and mutators. |
| `SettingsScreen_General.cpp` | 1325 | The Devices, Appearance and Behaviour panes only. |
| `ExchangeView.h/.cpp` | 24/1392 | The mapping-exchange tab: browse, detail, diff, install, publish, sign-in. |
| `ManualView.h/.cpp` | 34/757 | Focused Markdown renderer for the embedded `docs/user-manual.md`. |
| `ThemeBridge.h/.cpp` | 73/138 | Three ImGui palettes (Vanilla, Dark, Light) pushed and popped per frame. |
| `MixerLayout.h/.cpp` | 25/33 | Phase 2.6 Plug-in Mixer scaffold. **No call site.** |

### extension, other

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | FetchContent for reaper-sdk + WDL, pkg-config or root-var libusb/hidapi, seven embed steps, per-platform bundling and signing, frame-size guard, five ctest targets. |
| `cmake/embed_factory_bundle.cmake` | Turns any file into a `const char[]` header. |
| `cmake/write_commit_count.cmake` | git describe into `commit_count.h`, short-circuits on no change. |
| `jsfx/rea_sixty_input_level.jsfx` | True-input probe for the UC1 input meter. Embedded and auto-deployed. |
| `jsfx/rea_sixty_gr_probe.jsfx` | Envelope-follower GR probe. **Orphaned**, superseded by the impersonator. |
| `scripts/rea_sixty_assignment_hud_imgui.lua` | Learn HUD: surface mockup mirroring the focused plug-in's assignments. 4128 lines. |
| `scripts/rea_sixty_focused_panel_imgui.lua` | Frameless focused-track panel. |
| `scripts/rea_sixty_inserts_overlay.lua` | Non-destructive highlight of the active CS/BC instance in the MCP inserts list. |
| `scripts/rea_sixty_mode_banner.lua` | Transient banner on selection-mode or encoder-mode change. |
| `resources/factory.rea60config` | Frank's canonical setup, embedded for "reset to factory". |
| `resources/rea_sixty_winusb.inf` | WinUSB driver INF, embedded and fed to pnputil from Settings. |
| `resources/surface_maps.json` | Surface control geometry used by the exchange control tables. |
| `vendor/reaimgui/reaper_imgui_functions.h` | ReaImGui API binding header; the implementation is the user's ReaPack-installed module. |
| `tools/uf8_color_test.cpp` | Standalone colour-command probe. |
| `tools/uf8_palette_probe.cpp` | Standalone palette sweep. |
| `tools/uf8_hid_probe.cpp` | Standalone hidapi report dump. |
| `tools/ssl_core_probe.cpp` | Standalone Core-impersonation handshake probe. |
| `tools/gen_control_table.py` | Generates and checks the server's control tables against the surface map. |
| `version-names.tsv` | Tag to release-codename table, baked into the About tab. |

### server (api.reasixty.com)

| Path | Purpose |
| --- | --- |
| `src/app.js` | Fastify app factory. Explains the two-credential model (device bearer token for the extension, session cookie for the site) and why the website proxies `/auth` and `/v1` onto its own origin (WebAuthn RP-ID binding). |
| `src/server.js` | `listen()` wrapper. |
| `src/db/schema.sql` | accounts, credentials, sessions, tokens, challenges, vendors, plugins, maps, map_bindings, uf8_slots, uf1_slots, ext_funcs, works_for_me, reports, device_grants. |
| `src/db/index.js` | better-sqlite3 handle plus `migrate()`. |
| `src/routes/{browse,maps,upload,auth,account,admin,community}.js` | The HTTP surface. |
| `src/ingest/{merge,service}.js` | `.rea60map` parse, normalise, dedupe by content hash. |
| `src/query/{browse,detail,diff}.js` | Listing, per-map detail, two-map comparison. |
| `src/lib/{auth,session,webauthn,mail,vendor,controls,rea60map,reaper-scan}.js` | Credentials, passkeys, magic-link mail, vendor aliasing, control tables, envelope validation. |
| `src/tools/*.js` | mkadmin, mktoken, backup, backfill, mail check. |
| `test/*.test.js` | 12 node:test suites (auth, upload, dedup, vendor aliasing, param coverage, uf1 migration, route smoke). |
| `Dockerfile`, `docker-compose.yml`, `backup*.sh` | Deployment onto the Hostinger VPS behind Caddy. |

### clients

| Path | Purpose |
| --- | --- |
| `streamdeck/com.reasixty.companion.sdPlugin/plugin.js` | Elgato plugin: WebSocket to the Stream Deck app, raw TCP to the bridge on 127.0.0.1:49900. Uses the `ws` package. |
| `companion/src/main.js` | Bitfocus Companion module instance: config, polling, state. |
| `companion/src/bridge.js` | The NDJSON TCP client. |
| `companion/src/{actions,feedbacks,variables,presets,meter,upgrades}.js` | Companion surface: live builtin catalogue, feedbacks, variables, graphical meter bar, presets. |
| `tools/companion-to-streamdeck.html` | Converts a Companion config into Stream Deck profiles. |

### docs and analysis

`docs/` holds the living protocol notes (`protocol-notes.md`,
`protocol-notes-uc1.md`, `protocol-notes-uf1.md`), the user manual (baked into
the binary), install guides, the architecture decision, the interop rationale,
the capture workflows, and per-feature plans and handoffs.

`analysis/` holds the Python decoders that turned captures into the `.inc`
blobs and the tables in `Protocol.cpp`: `gen_init_sequence.py`,
`gen_uf1_init_sequence.py`, `parse_usbpcap*.py`, and roughly 20 UF1-specific
decoders (goniometer, VU needle fit, EQ alignment, loudness bars, RTA, timecode).

`captures/` holds the reference `.pcap`/`.pcapng` files. Most are gitignored;
the ones that are tracked are the ones a decoder still reads.

`dist/` holds every shipped artifact plus `release-{mac,linux}.sh`,
`release-win.ps1` and `check-linux-abi.py` (the guard that fails packaging if
the glibc floor regresses).
