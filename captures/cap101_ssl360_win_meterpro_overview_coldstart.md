# cap101_ssl360_win_meterpro_overview_coldstart

**Date:** 2026-07-15. **Host:** StoerPC (Windows), USBPcap3, UF1 = usb.device_address 14
(re-enumerated from 13 after SSL 360 restart). Claude-driven capture per capture-workflow.

**Action:** SSL 360 QUIT first (cold start is the point). Capture window 150 s, then Frank:
started SSL 360 (t≈26), focused a REAPER track with **MeterPro**, UF1 → Meter → **Overview**
(t≈35.8), played audio ~30 s. **Frank confirmed the goniometer DREW during this capture.**
This is the first capture that is Frank's own plugin + config with the goniometer verifiably
rendering — the ground truth cap75/76/89 (foreign sessions) never were.

## Findings

- **EP0 vendor init == cap84 exactly** (reset+modem-status ×2 ~150 ms apart, 8N1, flow none,
  baud 0xc068/0x0200 ×3, latency 2 ms, purge RX ×6, purge TX). Our replication (`2bf2789`)
  is byte-correct. EP0 is therefore NOT the goniometer gate.
- **Steady Overview cycle @ 24.6 Hz, exactly cap89's element order, and NO 0x0119 ever:**
  `img34..img0 0009 000a 0015 0016 011c 0125 0126 0127 0128 011d`.
- **Cycle VALUES (t=60–120, 1469/1469 cycles): 0009=ffff0000, 000a=00000000, 0015=ff,
  0016=ff.** cap75/76's 00 was that foreign session's state. Per-view state: the Analogue
  screen's 0x0009 carries VU-number colours (1e1e…) instead.
- **0x0128 is LIVE on Overview** — 0a/0f/0e (L/R latch + flashes) with hot audio. cap89's
  constant 00 was quiet audio, and cap98's "no overload on Overview" was wrong as a law.
- **0x011d closes EVERY cycle in EVERY view; its VALUE is view-state:** 0x00 on Overview,
  0x11 in the channel-view idle cycle (`0009 000a 0015 0016 011c 011d` @ 25 Hz).
  (cap84=0x11, cap77=0x19 fit: per-view/per-state values, not a constant.)
- **Overview ENTRY group (t=35.86):** 0100=0400, 0102=01, 0104×4 (labels), 0110=0f,
  010d=0a0b0a.., 010e×4, 011a=07, 011e=10, 0129=ff, 011f=00, **0125/26/27=(0,0), 0128=00 —
  then the first image burst immediately. NO 0009-group write, NO 011d in the entry.**
  SSL's first 011d after a view change comes at the end of the first image cycle.
- `ff54 0054` appears ONCE at t=34.08 (1.7 s before view entry); cap75 has no ff54 at all →
  not a view-entry requirement, deprioritised.
- Idle (plugin mode, channel view, before meter view): SSL streams
  `0009(ff..) 000a 0015(ff) 0016(ff) 011c 011d(11)` at ~25 Hz — the universal cycle shape.

## Consumed by

Commits `a455e29` (cycle order/data-driven — validated by this capture) and the follow-up
that set the cycle values to cap101's and made 0128 live. See docs/HANDOFF-uf1-goniometer.md.
