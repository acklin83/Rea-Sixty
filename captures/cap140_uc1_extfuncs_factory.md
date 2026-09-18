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
- **HQ is on all four SSL strips.** The runbook guessed it was 32C-only and
  would need a third `special`; measured, it is everywhere.
- **The SSL strips' lists are far shorter than the 32C's** (13–18 against 28)
  and contain none of the 32C's EQ/COMP/GATE section entries. The lists really
  are authored per strip, which is the whole reason for this capture.

## ⚠ Bus Compressor 2 — not resolved by this capture

Frank, right after the run: *"BC2 hat keine EXT FUNCS."* What the wire shows
from 106.3 s: the strip is selected, `PLUG-IN | COMP MIX` appears, the
`EXTENDED FUNCTIONS` title appears at 109.0 s, and the window **never scrolls**
before the menu closes at 111.1 s.

⛔ That is consistent with two different facts and does not choose between
them: a list that is exactly two entries long, or a menu that was opened and
not walked. The rolling window looks identical at "start of a long list" and at
"whole short list". A second 20 s capture with one deliberate scroll attempt on
BC2 decides it; until then, no BC2 table gets written.
