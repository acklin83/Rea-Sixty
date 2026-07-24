# UF1 main-LCD time display (element 0x0119) — DECODED, 2026-07-24 (StoerPC)

Frank asked what happens to the **Takt/Zeit display on the NORMAL (Channel) display**
— not the meter/analyzer. Investigation: the UF1 Rev4.0 manual says the LCD shows "the
DAW's time display" and there's an "UF1 TIMECODE DISPLAY: on/dim/off" setting. SSL 360
drives it; **our native build does NOT** (`uf1PaintChannel_` paints track/plugin/EQ, no
time element; the only timecode in our code is the unrelated UF8 per-strip `nav_lower_row`).
So this is a real, separate gap in "replace SSL 360 on the UF1." Found + fully decoded
here while the rig was up.

## Element 0x0119 = an 11-character 7-segment time display
`FF 67 <len> 01 19 <11 bytes> <cksum>`. Each of the 11 bytes = ONE character position.
It mirrors EXACTLY what REAPER shows in its transport (Frank: "er zeigt das an, was
reaper im transport anzeigt"). Only appears in the Channel/normal view (in the meter
view the LCD shows meters). Was flagged "codec unknown" in the meter notes — resolved.

### The encoding (verified across all 6 formats)
```
byte = (SEG7[digit] << 1) | dp
  0x00           = blank
  dp (bit 0)     = the separator DOT after this position
  SEG7 (standard 7-seg a=0x01…g=0x40):
    0=0x3f 1=0x06 2=0x5b 3=0x4f 4=0x66 5=0x6d 6=0x7d 7=0x07 8=0x7f 9=0x67
    (9 has no bottom segment → 0x67; so '0'→0x7e, '1'→0x0c, … '9'→0xce as payload bytes)
```
**No colon or special glyph exists** — every separator (`:` in H:M:S:F, `.` in
seconds, the beat dots in Measures/Beats) is just the **dp dot** on the preceding
digit. Confirmed on the H:M:S:F capture: distinct non-digit/non-blank bytes = NONE
(Frank: "hatte nur punkte"). Idle/reset = "1.1.00" = `…00 0d 0d 7e 7e 00…` (the same
0x0119 value the meter entry burst sends).

### Per-format layout (right-aligned into the 11 positions; blanks = 0x00)
| REAPER format | capture | example decoded |
|---|---|---|
| Measures/Beats | cap123 | `1.1.00` style (bar.beat.tick, dp between) |
| Minutes:Seconds | cap124 | `M SS.f` — byte: min, sec-tens, sec-units(dp), frac |
| Seconds | cap125 | `SS.f` (font cracked here: fraction cycles all 0-9) |
| Samples | cap126 | `104272` → counts up ~4096/block, right-aligned |
| H:M:S:F | cap127 | `00.00.01.02` (HH.MM.SS.FF, dp between groups) |
| Absolute frames | cap128 | `0,1,4,7,10,…` frame counter, right-aligned |

## Build (Mac, later — separate from Loudness)
In `uf1PaintChannel_` (or a dedicated paint step), when the UF1 is in Channel view:
read REAPER's transport position as the string REAPER itself shows — use
`format_timestr_pos(pos, buf, mode=-1)` (mode -1 = the project's current time-display
setting, so it matches automatically) — then map each character to a 0x0119 byte with
`encode_char()` and send the 11-byte element. Respect an on/dim/off preference like SSL's.
Update at ~playback rate. Decoder/encoder: `analysis/uf1_0119_timecode_decode.py`
(`decode_byte` / `encode_char` / `SEG7`).

Captures: `captures/cap123_uf1_timecode_measbeats.pcap`, `cap124..cap128_uf1_tc_*.pcap`
(gitignored). Element is Channel-view-only; needs the transport RUNNING to populate.
