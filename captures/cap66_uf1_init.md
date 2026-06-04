# cap66_uf1_init

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025) — **re-enumerated to dev 12** on `\\.\USBPcap1` after SSL 360 restart
(was dev 11). Endpoints unchanged (0x02 OUT, 0x81 IN).
**Action:** Frank fully quit + relaunched **SSL 360 (SSL360Gui.exe)** from the Start menu while
capturing → captured the UF1 cold init handshake from scratch.
**Duration:** 90 s. Init onset at **t≈14 s** (404-frame burst vs ~105/s steady after).

## Why / how (operational lessons)
- To re-trigger init we restart SSL 360. **NEVER restart SSL360Core from SSH** — it launches in
  session 0 and does NOT drive the UF1 (left it dark; first attempt failed, UF1 went dark until
  Frank relaunched the GUI). **Frank launches SSL360Gui from his desktop session.**
- SSL 360 restart **re-enumerates the UF1** → its USBPcap device number changes (11→12).
  Re-check the device number at the start of any post-restart capture.

## Init handshake structure (OUT 0x02), t≈14 s
1. **Wake/mode opcodes** (zero-payload `FF <op> 00 <ck>`): FF01, FF02, FF05, FF4B, FF4E,
   then FF47, FF4F, FF2D, FF1F, FF1E, FF1D (single occurrences).
2. **Per-LED init sweep** — for each LED id a triplet:
   `FF 38 04 <id> 00 00 <v> <ck>` + `FF 39 04 <id> 00 00 <v> <ck>` + `FF 3B 03 <id> 00 <ck>`.
   Walks id 0x00, 0x01, 0x02 … (every LED set to an initial state).
3. **FF 67 screen paint** (216 frames) → steady-state loop.
Plus FF1B keepalive, FF1D motor engage.

## NEW opcode FF 38 (LED) — reconciles cap64/65
LED control in the init is a **FF38 + FF39 + FF3B triplet** per LED, not FF39 alone.
Canonical FF39 shape from init: `FF 39 04 <id> 00 00 <v> <ck>` (4-byte payload).
→ Re-verify the cap64/65 brightness-byte offset against this format (dim/bright ground-truth
from cap65 still holds at the value level; the exact byte position may need adjusting).
FF38's role (second colour plane? RGB component? config?) = TODO.

## Full init frame stream
Complete cold-start sequence (enumeration control transfers + app handshake + LED sweep +
screen paint) is preserved in `cap66_uf1_init.pcapng` for replay when building native UF1 output.
This is the UF1 analog of the UF8 752-frame init replay.
