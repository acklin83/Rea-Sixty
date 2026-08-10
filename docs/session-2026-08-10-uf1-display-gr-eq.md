# Session 2026-08-10 — UF1 display regions, GR instance identity, EQ curve

Branch `uf1-native-build`, everything committed and pushed.

## What shipped

| Commit | What |
|---|---|
| `331611d` | UF1 `0x0000` backdrop behind the SOFT-key label = the key's state light; `0x0120` wired to `AnyTrackSolo`. |
| `5dfe849` | Manual: nav-cross-per-jog-object table (the Jog chapter never had one) + the two UF1 display rows. |
| `d1b5d13` | Zone-sweep probe removed; its two findings moved into `UF1Protocol.h` and `uf1PageHeader_`. |
| `183347a` | DynaMount doc: it has been running since June, not "awaiting a test". |
| `4cb0bcb` | Gate meter picked by strip MODEL instead of port order. |
| `6ff9027` | …and by the strip's SETTINGS, which is the only thing that separates two strips of the same model. Trace capped at 100 MB. |
| `e6a4296` | Keep the opening parameter dump — it arrives before the UDP port exists. |
| `1858952` | UF1 draws SSL's own EQ curve instead of our parametric reconstruction. |
| `d61f086` | A CS-Switch swaps the plug-in under the slot: caches keyed on (track, fxIndex) were stale. |
| `1343f6a` | UF1 key 1 = REAPER 40340 "Unsolo all" (config v21→v22). |
| `bd7be10` | Fine becomes the `uf1_fine_toggle` builtin on key 2 (config v22→v23). |
| `b82a728` | EQ graph on learned strips: global switch + per-map tri-state (catalog v15→v16). |
| `ddb38c2` | UC1 detent census (measurement rig). |
| `1a91da0` | UC1 encoder acceleration ripped back out. |

## The two display regions — answered

`SOLO CLR 1` and `FINE CTRL 2` are **captions the firmware draws**; they are not
text we can send. What sits under them:

- **`0x0120`** — 1-byte flag, `0x00`/`0xff`. Non-zero makes the firmware draw its
  own "SOLO ACTIVE".
- **`0x0000`** — 2-byte backdrop behind the channel soft-key label. `0x01` lights
  it; the init parks it at `03 00`.
- `FINE CTRL`'s value stays `0x011c` field 4.

Found by a single-sweep probe (rotate one candidate per ~2 s, stamp its id into
the HOST cell). Two by-products worth keeping: **`0x011b` is a command element —
writing a payload to it tears the layout down and needs a restart**, and
`0x0100..0x011a` renders nothing at all.

The UF1 User Guide **p187** had the layout drawn all along. Read the manual before
the next capture hunt.

## Which stream belongs to which plug-in

Three rungs, most specific first, each deferring when ambiguous rather than
picking: **settings → model → FX-chain ordinal** (`resolveStripPort_`).

The settings rung exists because a 4K E and a 4K B are the *same plug-in binary*
with a different analogue type — `SlotIndex`, `PluginIdent`, `UniqueId` and
`SessionDataId` are identical across instances. What differs is what the user
dialled in, and the wire names of those parameters are literally the `id` field
of our `LinkSlot` tables, so both sides read the same list.

## Still open

- **UC1 LMF Gain feels slower than every other control.** Nothing in our code
  treats it differently — same path, same `kEqGainSpeed`, same range, no notch-slot
  collision. The census (`ddb38c2`) is built and deployed but has not run yet:
  turn HMF and LMF the same distance with `uc1_knob_count` armed and compare
  `wireSum`. Equal = the plug-in's mapping, lower on LMF = the pot itself.
- **SSL Meter instances have the same identity bug the strips had.**
  `steerAutoPort_` takes the first Meter on the track and `uf1PinnedMeterTrackFx_`
  calls `uf1FindMeterFx_(tr, 0, …)`, so with two Meters on one track the V-Pots
  edit one instance while the display streams the other. The fix runs the other
  direction (stream → FX) and needs a wire-id ↔ REAPER-param-index table for the
  Meter plug-in.
- **Comp GR deliberately left alone.** `GainReduction_dB` is read per FX index, so
  it is already instance-exact; moving it to the wire would gain nothing and risk
  the GR calibration.

## Housekeeping for next session

- `ssl_core_trace` and `cs_log` are still **1** in `reaper-extstate.ini`. Run
  `~/Desktop/rea_sixty_traces_off.lua` once (needs a REAPER restart for the
  sslcore one) and delete the script.
- `~/Desktop/uc1_detent_census.lua` stays until the LMF question is answered.
