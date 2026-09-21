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

## ⛔ KORREKTUR, noch am selben Tag: drei der fuenf sind keine Luecken

Frank, nachdem er die Liste gelesen hatte: *"ähm, also ich seh die vier
spurnamen über den v-pots schon heute in unserm DAW mode auf UF1 inkl. namen
und dB wert."*

Er hat recht, und es steht im Painter (`main.cpp`, DAW-Zweig des V-Pot-Malers):

```cpp
// DAW mode: the 4 V-Pots are volume faders for the window tracks
// (selected + next 3, or +4..+7 via the "5-8" group).
sendVpotParam(uint8_t(i), nm, formatDbReadout(vol) + "dB");
setBar(i, uf1VolToPos_(vol) / kUf1FaderMax, /*bipolar*/false);
```

Damit fallen weg:

* ~~Vier Spurnamen ueber den V-Pots~~ — **haben wir**, ueber `0x010e`
  (Name und Wert in derselben 19-Zeichen-Zeile).
* ~~DAW-Faders-Modus~~ — **haben wir**, genau so: vier Fensterspuren, Name,
  dB, Balken.
* ~~5-8-Taste~~ — **haben wir**, `uf1DawWindowStart_` mit der 5-8-Gruppe.

⇨ **Die Lehre, und sie ist die zweite desselben Tages:** das Handbuch sagt, was
das Geraet KANN. Es sagt nicht, was WIR schon tun. Eine Luecke ist erst eine,
wenn beide Seiten nachgesehen sind — und die zweite Seite ist der Code, nicht
die Erinnerung. Ich hatte im selben Vormittag erst SSLs Handbuch zu spaet
aufgemacht und dann unseren Code gar nicht ([[vendor-manual-is-a-source]]).

## Was wirklich bleibt

1. ~~`0x010b` als zweite Zeile pro Pot~~ — **verworfen, Frank 21.09.:**
   *"brauchen wir doch gar nicht und würde eh nicht gehen weil wir doch auf dem
   layout mit dem EQ graph sind!"* Und das ist der physische Grund: in Layout 3
   sitzt an dieser Stelle der **EQ-Graph** (`0x0122`). SSL schreibt `0x010b` in
   Layout 2, wo kein Graph ist. Bei uns waere dort kein Platz — und das ist
   vermutlich auch die Antwort auf die nie gelaufene Sondenmessung (Element 12).
   ⇨ Dritte Korrektur desselben Tages, und dieselbe Klasse: ich habe ein Feld
   aus einer FREMDEN Ebene in unsere uebertragen, ohne zu fragen, was dort schon
   steht.
2. **Modus-Farbcodierung** (`0x012b`). Haben wir nicht, und sie haengt an
   Layout 1 (am Geraet vermessen 21.09., `docs/uf1-layout-probe-runbook.md`).
   ⛔ Korrektur: hier stand „Flacker-Baustelle des Ebenenwechsels". Die gibt es
   nicht. Das UF1-Flackern ist seit August geloest (`1511dc0`: 0x0100 wurde bei
   jedem MODE/SCRUB/Encoder-Schritt neu gesendet; `deb68c2`: zwei Schreiber pro
   Zelle) und hatte mit einem gewollten Ebenenwechsel nichts zu tun.
3. **CHANNEL-Modus "Volume"** — die Systemlautstaerke, fuer Kopfhoerer am
   eingebauten Ausgang unterwegs. Klein und unabhaengig.
4. **Low Scribble** ist laut SSL in REAPER leer und ausdruecklich fuer Dritte
   gedacht. Eine Flaeche, die niemand benutzt.

⚠ Alles hier ist **aus dem Handbuch gelesen**. Was SSL beschreibt und was die
Firmware tut, war zweimal dasselbe und einmal nicht (`0x011b`).
