# Was SSLs UF1 laut Handbuch kann und wir nicht

Quelle: `docs/docs/SSL UF1 User Guide_Rev4.0.pdf`, Kapitel *REAPER with UF1*
(S. 134-144) und die Modusliste, die in jedem DAW-Kapitel wiederkehrt.

⇨ **Gelesen am 21.09.2026, nachdem ein halber Vormittag damit verbracht wurde,
Dinge abzumessen, die auf Seite 138 stehen.** Die Adressen stehen in keinem
Handbuch, dafuer war der Korpus da. Die **Bedeutung** stand die ganze Zeit hier.

---

## 1. Der MODE-Knopf hat FARBCODIERTE Modi

> The MODE key determines the operation of the large screen and its 4
> associated V-Pots. **Each mode is colour coded.**

Im **REAPER**-Profil sind es drei:

| Modus | Farbe | was die vier V-Pots tun |
|---|---|---|
| General DAW | **weiss** | Pans, Sends, Plug-ins — je nach V-Pot-Zuweisung |
| DAW Faders — "FAdr" | **gruen** | **die Faderpegel von vier Spuren**, mit dB-Text UND Balken |
| Meter Plug-in | **gelb** | das SSL-Meter-Plug-in ueber SSL 360 |

In Pro Tools und anderen VST3-faehigen Hosts kommt ein vierter dazu:
**Plug-in Control (cyan)**. Das erklaert, warum Frank die Balken in REAPER
**ohne** Farbe gesehen hat und warum `0x012b` in cap141 nie vorkommt: SSLs
REAPER-Profil faerbt sie nicht.

⇨ Und es erklaert die Palette, die wir am Geraet abgelaufen sind. Die vier
dokumentierten Modusfarben sitzen auf den **ungeraden** Indizes:
**1 weiss · 3 gruen · 5 cyan · 7 gelb.**

## 2. Top Scribble Strip — vier Spurnamen ueber den V-Pots

> **Top Scribble** — Displays the 6-character track name for tracks 1-4 or
> 5-8 of the current controller bank.

Genau das, wonach Frank heute frueh gefragt hat. Das Element dafuer ist
`0x010b` (indiziert, `{index} + Text`, aus cap141). **Wir schreiben es nicht.**

## 3. Die 5-8-Taste

> Assigns the V-Pots to control tracks 5-8 of the current controller bank.

Die vier V-Pots zeigen wahlweise die Spuren 1-4 oder 5-8 der Bank. Der
Tastencode ist bei uns als `btn::k5to8` (0x22) bekannt.

## 4. CHANNEL-Encoder-Modi

| SSL | wir |
|---|---|
| Fader Sel — Kanal waehlen, bankt am Ende automatisch weiter | haben wir (ChSelect) |
| `<>` — das MCU-Bankfenster verschieben | haben wir |
| Focus — **emuliert das Mausrad** ueber dem Element unter dem Zeiger | haben wir (EncFocus) |
| **Volume — die SYSTEMLAUTSTAERKE** | **haben wir nicht** |

Der Volume-Modus ist fuer unterwegs gedacht (Kopfhoerer am eingebauten
Ausgang). Klein, aber es ist ein dokumentierter Modus, den unsere Liste nicht
kennt.

## 5. Weitere Felder des grossen LCD, die SSL benennt

Soft Key Labels · Timecode (Bars/Beats **oder** SMPTE) · Channel Encoder Mode ·
Solo Active · **MCU Bank Window** (Nummer der ersten Spur im Fenster) ·
Soft Key Page · Top Scribble · Low Scribble (*"Using REAPER's standard MCU
control, this area of the display is blank. 3rd party REAPER MCU scripts may
write to this area."*) · FaderdB · V-Pot Readout Bar

⇨ **Low Scribble ist laut SSL in REAPER leer und ausdruecklich fuer Dritte
gedacht.** Das ist eine Flaeche, die SSL selbst freilaesst.

---

## Die Liste, nach Wert sortiert

1. **Vier Spurnamen ueber den V-Pots** (`0x010b`). Direkt das, was heute frueh
   gefragt war, dokumentiert, und wir haben das Element aus cap141.
2. **DAW-Faders-Modus**: vier V-Pots auf vier Spur-Faderpegeln, mit dB-Text.
3. **Modus-Farbcodierung** (`0x012b`), jetzt dekodiert. In REAPER faerbt SSL
   nicht — wir koennten es.
4. **5-8-Taste** fuer die vier Pots.
5. **CHANNEL-Modus "Volume"** fuer die Systemlautstaerke.

⚠ Alles hier ist **aus dem Handbuch gelesen, nicht am Geraet geprueft**. Was
SSL beschreibt und was die Firmware kann, ist zweimal dasselbe gewesen und
einmal nicht (`0x011b`).
