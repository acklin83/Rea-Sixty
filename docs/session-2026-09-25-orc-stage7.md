# Session 2026-09-25 (evening): ORC stage 7, and the side-car moves out of main.cpp

Commits `4a68823`, `c07e49e` on main. Extension deployed `eed846a9`.

## What ORC is now

- `ORC.app`, `LSUIElement`: a menu-bar item (who holds the UF1, whether TotalMix
  answers, Settings, Quit) and a native AppKit settings window with three tabs:
  Connection, Controls (the eight pot roles, jog target and steps), Colours
  (TotalMix colour to UF1 palette, with TotalMix' own colour names and the
  channels currently wearing each).
- No on-screen mixer. The controls are the UF1 (Frank: "die ganze Bedienung
  machen wir ja eh übers UF1").
- No Dear ImGui and no GLFW. The first attempt drew its own ImGui window; it
  looked like a tool, and `extension/vendor/` is gitignored, so CI could never
  have built it.

## The side-car is shared, not rebuilt

ORC's first painter was written from memory and was wrong on the device (no pot
labels, no colour bars, letters in the seven-segment field, bank 2 ignored,
SHIFT dead). Both halves of Rea-Sixty's RME side-car were therefore moved out of
`main.cpp` verbatim, and both programs run them:

| File | What |
|---|---|
| `src/RmeInput` | what a knob, key or the fader does to TotalMix |
| `src/RmeFace` | what the surface shows (the former `uf1PaintRme_` and small-display painter) |
| `src/RmeBuiltins` | `rme_dim`, `rme_mono`, `rme_speaker_b`, `rme_talkback`, `rme_show_window`, `rme_fader_main` |
| `src/Uf1Text` | value-line rules, soft-key text rule |
| `src/Uf1Spread` | V-Pot cells, EQ frames, header, blank channel zone, `enterLayout` |
| `src/Uf1Pacer` | the one cycle (`emitCycle`) and its timing |
| `src/UF1Protocol` | seven-segment font, fader constants |

Method: a probe translation unit with `-fsyntax-only` lists exactly what a block
depends on (input: 18 names, painter layer: about 35, none of them the REAPER
API). State went into caller-owned structs (two encoder remainders and all the
painter's function statics included), host questions became callbacks, frames
go to a sink. Every changed line was diffed against the original.

What stays with each host: the soft-key emitter and its lamps, the button LEDs,
the MODE menu and the pacer loop. The extension shares those with REAPER's own
UF1 mode and they read REAPER toggle states. **ORC has no soft-key lamps yet.**

## Keys

ORC now calls the bindings engine the way `onUf1Event` does: STRIP page keys,
side-car soft-key banks, side-car keys, and everything else to `dispatch()`.
SHIFT runs through `mod_shift` from `orc.json`; fine mode reads
`modifierHeld(Shift)`.

## Handover between REAPER and ORC (Frank's option c)

New in Rea-Sixty, Settings → Devices → Connected devices, macOS only:
**"Take the UF1 over from ORC"** (default on). When on, Rea-Sixty writes its pid
to `~/Library/Application Support/ORC/handover` and retries a UF1 that did not
open every 5 s. ORC checks the marker twice a second, lets go while it names a
live process, and takes the UF1 back when REAPER quits. A stale pid is ignored.

## Tests

`test_rme_input` (seven rules), `test_rme_face` (layer, labels, colour bars,
time field), `test_orc_paint` gained the time-field check, `test_protocol` pins
palette names against palette entries. Each was broken on purpose and failed.
ctest 16/16. `check_builtin_docs.py` now reads `RmeBuiltins.cpp` too.

## Open

1. **Frank on the device:** the RME side-car in REAPER after the painter move.
   Not seen yet: at the end of the session no SSL device was on the USB bus.
2. Soft-key lamps in ORC (share the soft-key emitter), after 1.
3. Frame-trace comparison, Rea-Sixty against ORC (`ORC_TRACE=1`, same
   `reaper_uf1_frames.log`).
4. Sign and notarise `ORC.app`; set a deployment target; confirm the bundle id
   (`ch.stoersender.orc` is provisional).

## Later the same evening

Commits `d43355f` to `dd0133f`, extension deployed `bc5ffed8`.

- **Handover takes the TotalMix port too** (`d43355f`). ORC held UDP 7006 while
  REAPER had the surface, so the side-car showed "RME no TotalMix". ORC now
  stops its link when it yields and starts it when it takes the UF1 back.
- **Meter view names after MODE** (`a8060fb`). The 03.09. restore sat behind a
  gate that excludes MODE edges on purpose and never fired on its own. The
  release tick now re-sends the meter screen's four labels, nothing else.
- **ORC soft-key lamps** (`a8060fb`): `src/Uf1SoftKeys` (emitter, bank cell,
  colour helpers, verbatim from main.cpp); the engaged-state resolver moved into
  the bindings engine with `Host::toggleState` for REAPER actions.
- **V-Pot bank follows** nav left/right and the channel encoder
  (`c7ab4a1`, `7f49a76`, `RmeInput::followBank`).
- **Step 1: the RME settings are ORC's** (`c963b51`). Rea-Sixty reads
  `~/Library/Application Support/ORC/rme.json` and never writes it; no ORC, no
  RME side-car. Settings → Modes → RME is removed.
- **Step 2: Items jog mode "Fader = Item Volume"** (`af4fd2c`), replacing the
  Item Volume side-car: every selected item, proportionally; "no item" when none.
- **RME on the first key** of the SHIFT+MODE page (`dd0133f`).

### Open

1. The RME label on the SHIFT+MODE page was not drawn (pressing worked). Cause
   not found; moving RME to the first key restores the frame order that worked
   before. If it persists, trace it (`rea_sixty_uf1_trace_on.lua`).
2. Step 3: RME soft-key banks and builtins from `orc.json`, with a soft-key
   editor in ORC. To be planned in full first.
3. Frame-trace comparison, signing `ORC.app`.
