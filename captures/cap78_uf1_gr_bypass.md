# cap78_uf1_gr_bypass

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode, audio through a working compressor, then toggled Bypass.

## Dynamics GR meter — decoded (channel-info zone, analogous to UF8)
- **`0x0009`** (len 6, payload `<gr><gr> 00 00`) = **gain-reduction meter**. Two equal level bytes
  (mono comp → both same), animated with audio. Values seen 0x15→0x0f… = GR depth.
- **`0x000a`** (len 6) = second GR strip (gate) — idle `00000000` here (gate not reducing).
- **`0x0015`** (len 3, single byte 0x00–0x0c) = a further animated meter value (I/O level dot?).
- `0x000a`, `0x0016` constant ⇒ the two GR strips are comp(0x0009) + gate(0x000a), per UF8 pattern.

## Bypass — button LED only (confirmed by Frank)
No screen-element change on bypass (no "BYPASS" text frame, no discrete FF67 toggle). **Frank
confirmed: bypass is indicated via the button LED only**, not a screen element.

## Status
GR meter done. The Plugin-Mode **Channel view is now essentially fully decoded** (large LCD:
0x010e/0x010f/0x0122/0x0104/0x011c; small LCD: 0x000b/c/e/f + 0x0017 + GR 0x0009/a). Remaining
overall: Meter-mode graphic codecs (Overview/Analogue/RTA), timecode, bypass confirmation.
