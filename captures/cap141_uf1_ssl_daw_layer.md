# cap141 — UF1, SSL 360 im DAW-LAYER (nicht Plug-in Mixer), 2026-09-21

60 s USBPcap3, StoerPC, SSL 360 mit REAPER als Host, UF1 auf SSLs Treiber
(`oem16.inf`). Frank hat zweimal vom Plug-in-Layer in den DAW-Layer und zurueck
geschaltet, dazwischen V-Pots bewegt, Spuren gewechselt, Soft-Keys geblaettert.
6.7 MB.

**Warum:** der ganze bisherige UF1-Korpus (cap52 bis cap140) ist
**Plug-in-Mixer-Layer**. SSLs DAW-Layer war nie aufgezeichnet, und damit fehlte
uns jede Eintrittsfolge ausser der des Meter-Views.

## Der Befund in einem Satz

**SSLs DAW-Layer ist Layout 2**, und der Wechsel ist **zweistufig**:

```
0x0100 <- 00 01      (zweimal)
0x0100 <- 02 00      Ziel
```

Unsere Sonde hatte immer direkt umgeschaltet — deshalb blieb Layout 2 leer.

## Die Eintrittsfolge fuer Layout 2, byteweise

```
0x0009  00000000                  Pegel loeschen
0x000a  00000000                  Peak loeschen
0x0100  02 00                     LAYOUT
0x0102  00
0x0104  00 "F1" · 01 "F2" · 02 "F3" · 03 "SMPTE/BEATS"
0x0110  0f
0x010b  00 "1" · 01 "2" · 02 "3" · 03 "4"
0x010d  04 04 04 04
0x010e  00 · 01 · 02 · 03         (leer, nur das Indexbyte)
0x010f  2000 2000 2000 2000
0x0119  <timecode>
0x011a  02
0x011b  7e 0c
0x0120  00
0x011c  "FADER SEL ... 1/10"
0x011d  fb
```

Danach ein Ruhezyklus aus `0x0009`, `0x000a`, `0x011c`, `0x011d`.

## Drei Sachen, die daraus folgen

1. **`0x010b` ist ein Textfeld pro V-Pot**, indiziert wie `0x010e`. SSL schreibt
   dort die Kanalnummern. Wir kannten das Element nicht.
2. **`0x010d` kennt einen fuenften Stil, `0x04`.** Bekannt waren 0x01, 0x02,
   0x03, 0x08.
3. ⛔ **`0x011b` bekommt hier ZWEI BYTES** (`7e 0c`). Unsere Notiz "if it is
   ever wanted, send it EMPTY, the way SSL does" war aus dem Init
   verallgemeinert und ist korrigiert.

## Was cap141 NICHT zeigt

**`0x012b` kommt nicht vor.** SSL benutzt die vier Farbbalken im DAW-Layer
nicht. Was Frank dort als Balken in Erinnerung hatte, sind mit hoher
Wahrscheinlichkeit die `0x010f`-Balken im Stil `0x04` — er hat selbst
korrigiert, dass sie **keine Farbe** haben. Die farbigen Balken aus Layout 1
benutzt SSL also nirgends, wo wir bisher hingesehen haben.
