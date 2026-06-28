# UC1 scribble encoding = Latin-1 (ground-truth capture 2026-06-28)

USBPcap capture of SSL 360° → UC1 (PID 0023) on the Windows box, track named
`Bä Ö Ü ß`. Method: full re-enumeration forced by physically replugging the UC1
USB cable while capturing on the right root hub (USBPcap only hooks a device's
pipes on fresh enumeration — a soft PnP disable/enable was NOT enough; only a
physical replug produced the device traffic).

## The two name frames (UC1, dev addr 39, EP 0x02 OUT)

Small triple:
```
FF 66 25 02 | prev[12]=00… | curr[12]=42 e4 20 d6 20 dc 20 df 00 00 00 00 | next[12]=62 fc 31 00… | CK=33
```
Large triple:
```
FF 66 2B 04 | prev[14]=00… | curr[14]=42 e4 20 d6 20 dc 20 df 00…(6) | next[14]=62 fc 31 00… | CK=3B
```

- `42 e4 20 d6 20 dc 20 df` = **B ä [sp] Ö [sp] Ü [sp] ß**
- `62 fc 31` = **b ü 1** (the abbreviated/short name in the `next` slot)

Umlaut byte values: **ä=E4  ö=F6  ü=FC  Ö=D6  Ü=DC  ß=DF** → **pure ISO-8859-1 / Latin-1.**
No HD44780 codepage, no UTF-8, no custom map. Latin-1, exactly.

## Frame layout = byte-identical to our builder

`uc1::buildTrackNameTripleSmall/Large` (UC1Protocol.cpp ~560) already emits:
`FF 66 25/2B` + data[0]=0x02/0x04 + 3 × 12/14-byte null-padded slots (prev/curr/next)
+ checksum = `sum(bytes after FF, excl. CK) & 0xFF`. The capture confirms every
field, slot width, slot order, and the checksum algorithm. Nothing to change there.

## Our fold path is also correct

`utf8ToLatin1` (TrackName.cpp:10) maps UTF-8 ä (C3 A4) → 0xE4 etc. — correct.
The UC1 refresh() track-name path folds via `abbreviateTrackName_(…, foldLatin1=true)`
(UC1Surface.cpp ~4549-4565, cached into csCarouselCurrName_ 4806, steady-state 4810).
So with HEAD (13e6139 Latin-1 fold) the UC1 should render umlauts correctly. The
old handoff's "Latin-1 failed" predated this capture and an unfolded interim
(e3a04fa put UC1 back on raw UTF-8 → "Ã¤"). **Encoding question = CLOSED.**

If a UC1 umlaut still renders wrong on a CURRENT build, it is a specific *unfolded
emit path*, not the encoding — capture our own extension's bytes the same way and
diff against the above.

## Bonus: the small text field above the track name (`FF 66 05`)

The little field Frank pointed out (shows "4K B"/"4K G"/"MAIN" — same display area
as the carousel header but smaller) is its own sub-command:
```
FF 66 05 <pos> <ASCII…> CK
FF 66 05 01 4D 41 49 4E 91   = "MAIN"   (pos 0x01)
FF 66 05 10 34 4B 20 47 61   = "4K G"   (pos 0x10)
```
`FF 66` display, sub-cmd 0x05, one position byte, then short ASCII text. Likely the
EQ-type / channel label ("4K G" = SSL 4000 G-series EQ). Not yet wired in our code —
candidate for a future "EQ type / strip label" indicator.

Also seen idle/empty: `FF 66 4C 06` + 73×00 + CK (wide field, subtype 0x06).
