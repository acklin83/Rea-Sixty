# 2026-08-29 — The UF1 froze on the master, and nobody was painting

Frank: *„mir friert die ganze Anzeige ein wenn ich den Master anwähle"* — bars,
needle, readouts and the small channel peak meter, all standing still until a
turn of the channel encoder brought them back. It had survived a full day of
measurement, seven stages ruled out and five theories disproved, and it was the
one stage nobody had instrumented.

Confirmed fixed on hardware, both platforms. Later the same day, on the second
case: *„ja war leer, kam alles wieder."* Shipped as v0.5.9.

## What was actually wrong

`uf1FocusedTrack_` can answer `nullptr`, and only for the master. Three lookups
go blank in the same instant:

1. `GetSelectedTrack` ignores the master **by definition** — the SDK header says
   so in as many words (`reaper_plugin_functions.h:2621`).
2. `GetLastTouchedTrack` points straight at it after the click.
3. The line that stops the strip sticking to the master, `if (tr == master) tr =
   nullptr;`, then discards that last answer too. That line is old and correct;
   it just had no floor under it.

And `uf1PaintChannel_` opens with `if (!tr) { sTr = nullptr; return; }`. So the
entire paint was dropped every tick: the strip, the meter block, and the cycle
snapshot with them. The pacer, which is a separate thread, went on restating its
**last** snapshot at 25 Hz.

## Why every measurement said the system was healthy

This is the part worth keeping. The exclusion table from the handoff reads
backwards without a remainder:

| What was measured | Why it looked fine |
|---|---|
| Plug-in data (`seq`, `db`) | The plug-in never stopped. Nobody was reading it. |
| Snapshot `img=35 tail=6` | From the last paint before the freeze. |
| Pacer `emitted=24..25/s` | That is exactly the pacer repeating a stale snapshot. |
| USB, firmware, device | Valid frames kept arriving. The same ones. |
| "Identical frames" in the trace | That was the proof, and it was read as an alibi. |
| The healing channel move "sends nothing extra" | It does not have to. It puts a real track back in the selection. |

## The lesson: a counter only proves what it counts

`paint=148 meter=4` sat in the log for hours. `g_uf1PaintRuns` increments
**above** that early return and `g_uf1MeterRuns` below it, so the divergence was
the whole answer.

Two skip counters had been added next to them — and both were wired to the exits
already under suspicion (the preset browser, and `!meterView`). The third exit,
the one at the very top and the only one the master triggers, had none. So the
instrumentation reported `skipPre=0 skipNM=0`, "nothing is being skipped", and
that reading was believed over a 148-to-4 divergence measured in the same line.

The heartbeat had the same shape of blind spot: it lives *inside*
`uf1PaintMeter_`, so during a freeze it could not print at all. A probe inside
the suspect region cannot report that region failing.

**Rule going in: list every `return` in the function first, then place counters.
All exits get one, or none do.**

## The second case, same mechanism one step out

With the last track deleted the resolver has nothing left to hold — the track it
was holding fails `ValidatePtr2` — so the same early return fires and the strip
sat on "CH 1" with its old levels.

The empty state is now painted properly: name, dB, value line, channel number,
readout bar, the firmware's own `0x0006` "channel populated" flag, SEL dark,
Solo/Cut resting, and a `0x0122` wipe for the large plane, which otherwise keeps
its last image when no chunks arrive. Every frame is one the strip painter
already sends for an unselected, unsoloed, unmuted track.

The cycle is deliberately **not** stopped. The firmware falls back to a lazy
redraw when the chain breaks, so a zeroed idle snapshot keeps streaming instead.
The edge bumps `g_uf1Gen` so a returning track re-sends the meter entry burst
rather than believing `0x0122` is still on the glass.

## Decisions

- **The last valid track is held, the master is not followed.** Following the
  master on selection was tried this morning and reverted (`0afa296`); holding
  what was already shown keeps display and action in agreement without adding
  behaviour nobody asked for.
- **The instrumentation stays in.** Asked and answered: *„lass sie drin."*
  `emitted` against 25 is the cheapest test of whether the painter is alive.
- **No README refresh for v0.5.9.** Nineteen fixes and no new functionality, so
  there is no new phase or headline feature for the Status paragraph.

## Also fixed on the way (all in v0.5.9)

- `slotUsable_` threw away all three level types after 300 ms of an unchanging
  value with the transport stopped, without ever checking the value was at the
  **floor**. A held note vanished (`324c384`).
- The master's two chains are two Meter instances; the peak holds keyed off the
  track pointer, which is the same for both. A negative track index also arrives
  as ten varint bytes and five were being read.
- `-120 dBFS` meant both "measured silence" and "nobody answered", and the VU
  needle's rest gate read the second as the first.
- An Overview with no data drew nothing, so the device kept its last image.
- Windows: `SO_REUSEADDR` let the impersonator bind next to a live SSL 360, where
  delivery is documented as indeterminate.

## Open

- The pacer will restate its last snapshot indefinitely whenever the painter
  stops feeding it. Both known ways in are closed; the mechanism itself is still
  there, and it is what turned a missing resolver into a hardware-looking fault.
