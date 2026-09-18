# cap140 — SSL's EXT FUNCS lists for the factory strips (2026-09-18)

USBPcap3, StoerPC, SSL 360 2.1.12, UC1 `UC-000604` at **device address 41**,
daisy-chained through the UF1's hub (UF1 = address 40). 27.4 MB, 241 s of
traffic; the window was stopped early because Frank was done at 111 s.

Frank walked the hidden BACK-menu once per strip, in this order: Channel
Strip 2, 4K B, 4K E, 4K G, Bus Compressor 2.

**Decode:** `python3 analysis/uc1_extfuncs_decode.py captures/cap140_uc1_extfuncs_factory.pcap --dev 41`

## How to read the walk

The UC1 shows a **rolling three-row window** into the list. Each step prints
the three visible entries, so the list is the sequence of entries entering the
window. The end is unambiguous: at the last step the window holds only **two**
rows, because there is no third entry to show. Every walk below ends that way,
so no list is cut short.

The strip in play is named in the track-name rows just before each walk
(`4K BM` at 24.0 s, `4K EP` at 50.0 s, `4K GR` at 77.6 s, `BUS COMP 2` at
106.3 s). ⚠ The first walk carries no such row — it is Channel Strip 2 by
Frank's stated order and by elimination, not by a name on the wire.

## The lists, as SSL authored them

**Channel Strip 2** (13): PLUG-IN · COMP MIX · FILT IN · PAN · WIDTH ·
OUT TRIM · SOLO SAFE · A/B · HQ · WIDTH MD · WIDTH FQ · AUTO MKP · MKP OFST

**4K B** (16): PLUG-IN · COMP MIX · PRE · MIC · FILT IN · PAN · WIDTH ·
OUT TRIM · SOLO SAFE · A/B · HQ · ANLG VCA · WIDTH MD · WIDTH FQ · AUTO MKP ·
MKP OFST

**4K E** (16): identical to 4K B, entry for entry, in the same order.

**4K G** (18): PLUG-IN · COMP MIX · PRE · MIC · **IMPED IN · IMPEDANCE** ·
FILT IN · PAN · WIDTH · OUT TRIM · SOLO SAFE · A/B · HQ · ANLG VCA · WIDTH MD ·
WIDTH FQ · AUTO MKP · MKP OFST

4K G is 4K B/E plus the two impedance entries, inserted after MIC.

**Bus Compressor 2** — ⚠ open question, see below.

## What this settles

- **SOLO SAFE and MKP OFST are new.** Neither is in `kExtFuncs32c`, which ends
  at AUTO MKP.
- **HQ is a menu entry on all four SSL strips.** Whether it is also a host
  parameter there is a separate question, and `docs/ssl-native-params/` cannot
  answer it: those dumps are from 2026-04-21, before SSL 360 2.1.12. Frank,
  2026-09-18: *"doch, der CS2 hat HQ Mode."* The dumps are stale, not the
  plug-ins — they need re-running before any table is written.
- **The SSL strips' lists are far shorter than the 32C's** (13–18 against 28)
  and contain none of the 32C's EQ/COMP/GATE section entries. The lists really
  are authored per strip, which is the whole reason for this capture.

## Bus Compressor 2 — no EXT FUNCS menu at all

Frank, at the device, twice: **BC2 has no EXT FUNCS menu.** That is the fact;
the wire only looked ambiguous because I was reading it wrong.

From 106.3 s the strip is selected and `PLUG-IN | COMP MIX` appears, with
`EXTENDED FUNCTIONS` at 109.0 s. None of that is a menu: `EXTENDED FUNCTIONS`
is the **key label**, printed 16 times across this capture including in the
middle of the other four walks, and `PLUG-IN` / `COMP MIX` are what the UC1
shows for the strip itself. A menu leaves a different trace — the rolling
three-row window scrolling — and on BC2 it never does, because there is nothing
to open.

⛔ **The lesson:** "the window never scrolled" is not evidence of a short list,
and it was not evidence of an unwalked one either. Only the person at the
device could say which, and he had already said it. No BC2 table exists,
because SSL wrote none.
