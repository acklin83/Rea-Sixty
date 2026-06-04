# cap84_uf1_plugin_init

**Date:** 2026-06-04. **Device:** SSL UF1 (PID_0025) = **usb.device_address 9 on USBPcap3**
(Windows reinstalled since cap66 — USBPcap assignment changed from 1→3, dev 12→9; memory's
"UF1=USBPcap1" was stale). Captured all 3 USBPcap interfaces simultaneously; only p3 had the UF1.
**Action:** Frank cold-started SSL360 (SSL360Gui) with the UF1 in **Plugin Mode**, no DAW track
focused. Init onset **t≈54s** (331-frame burst).

## Why captured
To get a Plugin-Mode cold init (cap66 was MCU mode) to fix the native "MCU look" + colour bar.

## Findings (resolved the view question — NO new init needed)
- **0x011d is NOT a view selector.** cap84=0x11, cap77=0x19, cap66=0xfb — too variable; it's a
  header/status byte, not MCU-vs-Plugin. (Streaming 0x011d=0x19 natively did nothing — removed.)
- **The init is mode-agnostic:** cap84's burst = same wake opcodes (FF01/02/05/4B/4E/47/4F/2D/1F/
  1E/1D) + LED sweep (55 triplets) + paints as cap66. Replaying cap84 buys nothing over cap66.
- **MCU vs Plugin is content-driven (steady-state):** cap66 (MCU) steady streams 0x0100/0x0106/
  0x011b; cap84 (Plugin idle, empty channel) steady = only meters (0009/000a/0015/0016) + header
  (011c/011d). The init paints 0x0100/0x0106/0x011b ONCE; in Plugin Mode SSL overpaints that main-
  display region with channel-strip content, in MCU it keeps refreshing them.
- **So the native "MCU look" = the init-painted 0x0100/0x0106/0x011b residue we never overwrite —
  likely occluding the colour-bar (0x0018) region.** Fix = paint the Plugin-Mode channel-strip
  main display (0x0104 soft-keys, 0x010e focused param, 0x0122 EQ) over that region. That's the
  proper Phase-2 channel-strip view, not a one-liner.
- Colour bar (0x0018) renders in cap77 with an SSL CS plugin ("4K E" @0x0017); unclear if it shows
  for generic tracks without that channel-strip context.
