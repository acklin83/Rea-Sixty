# UF1 time/position display (element 0x0119) — build spec

**Status: decode-only, nothing built.** Element `0x0119` exists only as a static idle "1.1.00" in meter setup bursts (`main.cpp:16106`, `:16132`) and is excluded from the meter cycle (`main.cpp:17102`). No `format_timestr*` call exists in the extension.

## The element
- **`0x0119`** — an **11-byte, one-byte-per-character 7-segment field**, right-aligned, `0x00` = blank.
- Per byte: `byte = (SEG7[digit] << 1) | dp`, `dp` (bit 0) = separator dot AFTER that position. **No colon/period glyph** — every `:` `.` `,` in REAPER's string becomes a `dp` bit on the preceding digit.
- `SEG7 = {0:0x3f,1:0x06,2:0x5b,3:0x4f,4:0x66,5:0x6d,6:0x7d,7:0x07,8:0x7f,9:0x67}`.
- Idle "1.1.00" = `00 00 00 00 0d 0d 7e 7e 00 00 00`.
- Written with existing `uf1::buildScreen(0x0119, payload11)` (`UF1Protocol.cpp:146`). Add `scr::kTimecode = 0x0119` to `UF1Protocol.h:189`.
- Reference decoder: `analysis/uf1_0119_timecode_decode.py:31-36` (encode direction). Full decode: `docs/session-2026-07-24-uf1-timecode-display.md`.

## Values from REAPER (API already linked, no import work)
```
const int    ps  = GetPlayState();
const double pos = (ps & 1) ? GetPlayPosition() : GetCursorPosition();  // mirror main.cpp:18920
char buf[64]; format_timestr_pos(pos, buf, sizeof(buf), mode);
```
`format_timestr_pos` modes (`reaper_plugin_functions.h:1380`): **MEASURES=2**, **TIME=0** (h:m:s.sss) or **5** (h:m:s:f), **SAMPLES=4**, **-1**=mirror REAPER transport.

## Build (Frank wants measures + time + samples → ONE switchable field)
- It's a single physical field → **switchable**, not simultaneous.
- Implement a UF1-side 3-state cycle: **Measures (2) → Time (5) → Samples (4)**, persisted like `g_navLowerRow` (`main.cpp:685`, persist `main.cpp:28125`). Add `g_uf1TcMode` atomic. Pick a free UF1 button/soft-key to cycle it (see the control blueprint's unbound ids — a NAV/sec-transport button or a soft-key). If no obvious button, default to mode `-1` (mirror REAPER) and expose the cycle later.
- **New code:** `uf1EncodeTimecode_(const char* reaperStr, uint8_t out[11])` — walk the string; digit → `(SEG7[d]<<1)`; separator (`:` `.` `,`) → set `dp` on the last pushed digit; right-align into `out[11]`, left-pad `0x00`; clamp negatives to 0 (no minus glyph); truncate >11 digits.
- **Hook:** `uf1PaintChannel_` (`main.cpp:17832`, from onTimer ~30 Hz), **channel-view branch only** (near the name/dB/pan sends `main.cpp:18053-18151`). Send-on-change via `static std::string sTc; static int sTcMode;` — reformat each tick, `send()` only when the 11 bytes differ (force on view/gen change). **Do NOT write 0x0119 in the meter branch** (keep the `:17102` exclusion).

## Gaps (HW-confirm later)
- Host→UF1 encoding never HW-driven — confirm right-align origin + refresh-during-playback vs meter pacer.
- on/dim/off intensity not decoded.
- edit-cursor-when-stopped is an assumption.
