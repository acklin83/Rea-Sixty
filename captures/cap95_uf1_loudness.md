# cap95_uf1_loudness

**Date:** 2026-07-14
**Device:** SSL UF1 (dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **LOUDNESS** screen (Meter Pro only).
**Driver:** SSL 360°.
**Signal:** `dbfs_sweep_mono.wav` — 1 kHz, 31 steps × 2.5 s, 0 → −90 dBFS, L=R.

First look at the Loudness screen — it had never been captured (Frank spotted the gap).
Exploratory: structure only, no value law yet.

## `0x011c` — 4 fields × 25 B, each = value + unit + label
```
[-11.8 LUFS Integrated] [-7.0 LUFS Short-Term] [0.0 dBFS True Peak Max] [-0.4 LUFS Short-Term Max]
```
Note this differs from the other screens' 6×25 B layout, and each field packs its own unit
and caption rather than relying on a separate label element.

## `0x0122` — a sub-frame selector, NOT the Overview's chunked buffer
`byte[0]` selects one of **4 sub-frames**, each sent 2332× in this capture (= the refresh
count):

| `byte[0]` | len | content |
|-----------|-----|---------|
| **0** | 251 | dB axis labels — `-14 -17 -20 -23 -26 -29 -35 -41` |
| 1 | 251 | empty |
| 2 | 251 | sparse |
| **3** | 208 | **the history plot** — 207 time columns as byte heights |

Sub-frame 3 carries the curve: the sweep renders as a clean monotonic ramp of byte values
(ASCII `!"#%')*,-./0135…~` = 33..126 rising), i.e. our level staircase. A separate frame
also carries the time axis (`-00:00:29 -00:00:19 -00:00:09 00:00:00`).

**This is a plain array of plot heights** — no variable-length rows, no bit packing. Far
simpler than the Overview Lissajous (see cap92-93).

## Open
- The **value → byte law** for sub-frame 3. Straightforward with the standard method: the
  LUFS numbers sit in `0x011c` in the same stream, so pair them against the plot column.
  Careful: the plot is a *time* history, so a column corresponds to a past moment, not the
  current readout — pair against the time axis, not the instantaneous value.
- What sub-frames 1 and 2 carry (empty/sparse here — likely the target/range bands).
- Loudness is **Meter-Pro only** and is deliberately out of our screen cycle
  (`kUf1MeterScreenCycle = 3`) until the impersonator surfaces `PluginType`. Its setup
  burst (`0x0100 = 04 05`) is already captured in `uf1MeterScreenBurst_`.
