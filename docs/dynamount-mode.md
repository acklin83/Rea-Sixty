# DynaMount Mode — status & plan

Control up to 8 DynaMount robotic mic stands from the UF8 surface.
Protocol was reverse-engineered & live-verified; reference material lives in the
`dynamount-sniffing` repo (`PROTOCOL.md`, `dynamount.py`, `REA-SIXTY-INTEGRATION.md`).

## Protocol (Gen1 — Frank's units, LIVE-VERIFIED)
- Plain HTTP/1.0 GET on port 80, single endpoint, no auth, Webduino firmware:
  `GET /move?h=<0-100>&r=<0-180>&v=<0-100>&s=9` → `{"result": "success"}`
- `h` = distance, `v` = left/right, `r` = rotation, `s` = speed (fixed 9).
  `roff` = rotation offset, `rst` = reset cmd (1 calibrate, 4 home).
- **Open-loop**: no position feedback — the controller owns the h/r/v state.
- Frank's mounts: `192.168.177.253`, `192.168.177.254`.
- Gen2 (Axial-2/RIZR): raw TCP :5000, `a<axial>f<focus>r<rot><speed>#` — scaffolded only,
  no hardware to verify; deferred to Phase 6.

## Done & on `main` (phases 1–3)
- `extension/src/DynaMountClient.{h,cpp}` — cross-platform raw-socket client (no REAPER/libusb
  dep). `gen1Move/Home/Calibrate/SetRotationOffset`, `detectPassive`. Connect-timeout so a dead
  IP fails fast (never stalls the main thread). namespace `uf8::dynamount`.
- `extension/src/DynaMountManager.{h,cpp}` — owns 8 `Mount` (atomic targets), ONE coalescing
  worker thread doing all socket I/O (~15 Hz/mount). Strip↔mount mapping (`mountForStrip`,
  `isDynaStrip`), fill Left/Right, calibration fader mapping (`setDistance`/`setHorizontal`/
  `nudgeRotation`, `faderNorm`), async `requestDetect`, `serialize`/`deserialize`.
  Process-wide singleton `uf8::dynamount::manager()` (lazy socket init + worker start).
- `extension/src/SettingsScreen.cpp` — "DYNA" sub-tab under Modes: per-mount enable/name/IP/
  color/Detect + fill-direction toggle. Persists to ExtState `rea_sixty`/`dynamount_devices`
  (+ `_fill`); SetupBundle captures it.
- `extension/tests/test_dynamount.cpp` — pure-logic unit tests (wire format, clamping, strip
  mapping, fill, calibration, serialize round-trip). CMake target `test_dynamount`.

Speed fixed global 9 (not user-exposed). Rotation clamps 0–180.

## Build / install (verified working)
```
cd extension
cmake build                                   # reconfigure if CMakeLists changed
cmake --build build --target reaper_uf8       # incremental
cmake --build build --target test_dynamount && ./build/test_dynamount
# native ext: copy to UserPlugins and RESTART REAPER (no hot-reload):
cp build/reaper_rea-sixty.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
```

## Phase 4 — UF8 mode (NOT started; the deep part)
Wire a dedicated **selection mode like REC**. Key decisions (Frank, locked):
- Only **N strips** (= enabled mounts) become DynaMount strips; the rest stay normal tracks.
- **Pinned** strips, fill **Left or Right** (Setting), pins **push** tracks (track-bank offset by N) —
  model on the existing **AUTO mode** pin/fill logic (`reasixty_autoFillFromRight`,
  the "fewer visible tracks than 8 strips" path) — that already does exactly this for tracks.
- **FLIP per-strip-type**: global FLIP latch; normal strips do normal FLIP, DynaMount strips
  swap axis `h`↔`v`. V-Pot always rotation `r`.
- Mount **name** → Trackname field; **colorbar** = per-mount color.

Integration hooks (from a prior code map, verify against current `main.cpp`):
- Selection modes: `enum class SelectionMode` (~`main.cpp:1009`), `g_selectionMode`,
  `selectionModeFriendly()`. Add a DynaMount mode + its activation button.
- Strip→track mapping & AUTO fill/pin: find where AUTO mode maps visible tracks to the 8 strips
  (the fill-from-left/right code) and branch so pinned strips render mounts instead of tracks.
- Fader input: `onUf8Input` fader path (~`main.cpp:13725`) — if `isDynaStrip(strip)`, route to
  `manager().setDistance/setHorizontal` (per FLIP) and DON'T forward to track volume.
- V-Pot input: `FF 24 06` path — DynaMount strip → `manager().nudgeRotation`.
- FLIP button handler — latch shared flip; re-drive DynaMount fader to the new axis.
- Feedback each tick: motor fader to `manager().faderNorm(idx, flipped)`, scribble top = name,
  bottom = `H.. R..`/`V.. R..`/`OFFLINE`, colorbar = mount color, banner = "DynaMount".
- Lifecycle: optionally create the manager explicitly in `REAPER_PLUGIN_ENTRY` and `stop()` in
  QUIT instead of relying on the lazy singleton.

Phase 5 = calibration wizard (drive to reference poses, record values, `rst=2` offset).
Phase 6 = Gen2 TCP path (needs hardware to verify field encoding).
