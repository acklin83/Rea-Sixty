# cap77_uf1_channel_info

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode, transport stopped.
**Action:** selected several tracks (named grün/blau/gelb/cyan), changed Pan, moved fader, toggled Bypass.

## Channel-info zone (0x00xx plane = the "small LCD" of manual p182) — decoded
| addr | len | element | evidence |
|------|-----|---------|----------|
| `0x000b` | 7  | **TrkNam** (track name) | showed the track names (grün/blau/gelb/cyan/…) |
| `0x000c` | 11 | **Output fader dB** (O/PdB) | "0.0 dB", "-0.1 dB", … (98 distinct = fader moves) |
| `0x000e` | 22 | **Pan** label+value | "Pan C", "Pan +5", "Pan +13", … |
| `0x000f` | 4  | **Pan readout bar** position | value byte sweeps |
| `0x0017` | 7  | **CS TYPE** | "4K E", "4K B" |
| `0x0014` | 4  | channel number / position? | 2,3,4,5 |

(NB: `0x000e` here = Pan in the channel zone; `0x010e` in the 0x01 plane = the plugin focused
param. Different planes, same low byte — the high byte 00 vs 01 selects the zone.)

- **DAW track colour** = the universal FF38 colour mechanism (cap70/74), for the channel-colour element.
- **Bypass** = NOT yet pinpointed (a small status frame — 0x0006 / 0x0009 / 0x0015 / 0x0016 / 0x0018
  carry no text; one of these likely toggles on bypass). TODO.
- **Dynamics GR meter** (gate + comp gain reduction) = needs audio through a dynamics section. TODO.

## Status
Channel-info zone (manual small-LCD layout) largely mapped. Remaining: Bypass flag + Dynamics GR
meter (audio). Combined with the Channel-Strip large-LCD (0x010e/0x010f/0x0122/0x0104) and the
header (0x011c), the Plugin-Mode Channel view is now nearly fully decoded.
