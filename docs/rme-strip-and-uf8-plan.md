# RME: Kanaleinstellungen auf dem UF1 und der Weg zum UF8

Stand 22.09.2026. **STRIP (Abschnitt 3) ist gebaut**, am Gerät noch nicht
getestet: `RmeStrip.{h,cpp}` (Katalog, Seiten, Schreiben), Seiten in `rme.json`
v4 (`"strip"`), Bedienung und Maler in `main.cpp` (`uf1RmeStripPage_`,
`uf1PaintRme_`). Abweichungen vom Plan: Output-Seite V-Pot 4 = Ref Level (sonst
hätte ein Ausgang keinen Platz dafür), Listen-Keys tragen ihren Wert im Namen
(„Type 1 Bell"), Input-Seiten nur für Eingänge und Playbacks (`rows`).
Abschnitt 4 (UF8) und 5 sind nicht gebaut. Setzt auf
`docs/uf1-spread-plan.md` auf (Monitor-Side-Car, Schritte 1 bis 3 gebaut).
Alles nach v0.6.0.

---

## 1. Was heute steht

| | Stand |
|---|---|
| OSC-Link `RmeManager`, Remote 3 (7005/7006), `rme.json` | gebaut, läuft. ⇨ Seit 25.09.: Einstellungen nur noch in ORC (`~/Library/Application Support/ORC/rme.json`), Rea-Sixty liest mit, die Seite Modes → RME gibt es nicht mehr |
| UF1-Side-Car „RME": 4 V-Pots (Phones 1-4), Fader = gewählter Kanal, drei Reihen, Submix, Jog = Main, EQ-Graph, Kopfzeile, Pegel | gebaut, von Frank getestet („läuft eigentlich alles") |
| eigene Soft-Key-Bänke + `rme_dim` / `rme_mono` / `rme_speaker_b` / `rme_talkback` | offen (Schritt 4) |
| **REC/RME-Modus** (seit 19.08., `9590d85`): UF8, UC1, UF1 fahren Gain, 48V, Pad, Phase, AutoLevel eines REAPER-Spur-Eingangs über **TotalReaper-Actions**, lesen den Zustand aus `P_EXT:totalreaper_*` | gebaut, im Einsatz |

⛔ **Damit gibt es ab jetzt zwei Wege zu denselben Preamps:** REC/RME über
TotalReaper (Remote 1) und das Side-Car direkt über OSC (Remote 3). Technisch
stören sie sich nicht: TotalMix meldet jede Änderung an alle anderen Remotes,
beide sehen also immer den echten Stand. Was fehlt, ist eine Entscheidung, ob
das so bleibt (Abschnitt 5).

---

## 2. Was ein Kanal einstellen kann

Nicht aus dem Gedächtnis: am 21.09. an Franks TotalMix auf Remote 3 abgefragt,
dazu TotalReapers `docs/osc-paths-discovered.md`.

| Parameter | Line-Eingang | Mic-Eingang (UFX+ 9-12) | MADI mit Preamp davor | Playback | Ausgang |
|---|---|---|---|---|---|
| `gain` | ja | ja | ja | | ja |
| `48v` | | ja | ja | | |
| `pad` | | | **ja** (`/input/38/pad`, TotalReaper) | | |
| `instrument`, `autoset` | | ja | | | |
| `reflevel` | ja | | | | ja |
| `phase` | ja | ja | ja | ja | ja |
| `stereo`, `width`, `msproc` | ja | ja | ja | ja | `stereo` |
| `fxsend` / `fxreturn` | ja | ja | ja | | `fxreturn` |
| EQ (3 Bänder), Low Cut | ja | ja | ja | | ja |
| Dynamics (comp/exp, attack, release, gain) | ja | ja | ja | | ja |
| AutoLevel (maxgain, headroom, risetime) | ja | ja | ja | | ja |
| `balpan`, `loopback`, `talkbacksel`, `crossfeed`, `delay` | | | | | ja |
| Room EQ (9 Bänder) | | | | | ja |

⇨ **Welcher Kanal was hat, sagt TotalMix, nicht das Gerätemodell.** Ein
MADI-Kanal bekommt Pad und 48V, wenn eine RME-Preamp-Box dahinter hängt
(TotalReaper hat das mit MADI 9 gezeigt). Die Seiten der Kanalansicht werden
deshalb aus den Adressen gebaut, die TotalMix für diesen Kanal meldet, und
nicht aus einer Tabelle pro Interface. `RmeState` muss dafür lernen, welche
Blätter ein Kanal hat.

**Kontrollraum**, zu keinem Kanal gehörig: `dim`, `mainmono`, `speakerb`,
`talkback`, `mutefx`, `externalin`, `dimreduction`, `recallvolume`, `linkab`.

---

## 3. Die Kanalansicht auf dem UF1 (STRIP)

### 3.1 Hinein und heraus

Druck auf den **Kanal-Encoder** öffnet die Einstellungen des Kanals, der auf
dem Fader liegt. Zweiter Druck schliesst. Der Druck ist im Side-Car heute frei
(abgefangen, tut nichts). MODE halten bleibt der Ausgang aus dem ganzen
Side-Car.

Fader bleibt der Pegel des Kanals, die kleine Anzeige bleibt Name und dB.

### 3.2 Seiten

◄ ► blättern, die Kopfzeile zeigt `EQ 1  3/8`. Nur Seiten, die der Kanal hat.

| Seite | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 | Soft-Keys |
|---|---|---|---|---|---|
| **Input** | Gain | FX Send | Ref Level | Width (nur stereo) | 48V · Pad · Phase L · Phase R (nur stereo) |
| **Input 2** | | | | | Stereo · M/S (nur stereo) · Inst · AutoSet |
| **Low Cut** | Freq | Slope | | | LC an |
| **EQ 1** | Gain | Freq | Q | Typ | EQ an |
| **EQ 2** | Gain | Freq | Q | | EQ an |
| **EQ 3** | Gain | Freq | Q | Typ | EQ an |
| **Dyn** | Threshold | Ratio | Attack | Release | Dyn an |
| **Expander** | Threshold | Ratio | Make-up | | Dyn an |
| **AutoLevel** | Max Gain | Headroom | Rise | | AL an |
| **Output** (nur Ausgänge) | Pan | Crossfeed | Delay | | Loopback · TB-Sel · Phase |

Belegung über dieselbe Datei wie der Rest (`rme.json`), damit Frank sie ändern
kann. Die Tabelle oben ist die Werksbelegung.

⇨ **Was eine Seite zeigt, hängt am Kanal, nicht nur am Gerät** (Frank 21.09.:
„das kommt doch drauf an, ob der Kanal auf stereo oder mono steht, siehe
TotalReaper"). `stereo` ist schreibbar, TotalReaper sendet es selbst
(`TotalReaperCSurf.cpp:1020`, Stereo-Pair Link). Mono: kein Width, kein M/S,
eine Phase. Stereo: Width, M/S, Phase links und rechts getrennt (Phase und Gain
gehen pro Seite, rechts = n+1, Abschnitt 5a); Gain dreht beide Seiten. Der
Stereo-Schalter steht darum immer da, und die Seite baut sich nach dem Schalten
neu. Nach einem Entkoppeln taucht n+1 als eigener Kanal auf: dafür beim Schalten
`/sendchan` für n und n+1 nachfragen, nicht auf ein Echo warten (TotalMix
schickt dem sendenden Remote keins).

Leere Soft-Keys bleiben leer. Hat ein Kanal kein Pad, steht dort nichts, und
die Taste tut nichts.

### 3.3 V-Pot-Schritte und Einheiten

Jeder Parameter braucht Schrittweite, Grenzen und Anzeige. Bekannt aus dem
Dump bzw. TotalReaper: Gain ganzzahlige dB, Frequenzen in Hz, Q echt, Typen
0..3, Slope 0..3. **Die Grenzen (Gain-Maximum, Ratio-Bereich, Attack/Release)
kenne ich nicht belegt.** Quelle wäre RMEs Tabelle `OSCProtocoll_260721.ods`
(liegt laut Memory bei Frank, nicht im Repo, auf dem Mac Studio unter
`~/Downloads` am 21.09. nicht gefunden). Frage 4.

Frequenz und Zeiten logarithmisch schrittweise (wie REAPERs Plugin-Pfad), dB
linear, Typen und Slope als Schalter.

### 3.4 Der EQ-Graph nur auf den EQ-Seiten (Franks Idee)

Das hängt an einer Tatsache vom Nachmittag: **der Graph existiert nur in
Layout 3, und in Layout 3 ist er immer da.** Ausblenden heisst also nicht
„Graph aus", sondern **die Ebene wechseln**. Zwei Wege:

| | ausserhalb der EQ-Seiten | Kosten |
|---|---|---|
| **A** | Layout 3 bleibt, der Graph zeigt eine flache Linie | nichts, aber der Graph ist nicht weg, nur flach |
| **B** | **Layout 1**: kein Graph, dafür die **vier Farbbalken** über den V-Pots, in TotalMix-Farben | Ebenenwechsel beim Betreten/Verlassen der EQ- und Low-Cut-Seiten; Stil `0x02`/`0x04` statt `0x01`/`0x08`; Wertzeile schmaler (14 Gross- / 18 Kleinbuchstaben) |

B bringt die Farbbalken zurück, die im Monitor-Teil schon einmal gewünscht und
wegen des Graphen gestrichen waren: Monitor-Ansicht auf Layout 1 mit den
Kanalfarben, Low Cut und EQ auf Layout 3 mit dem Graphen. Alles dafür ist
gemessen (Runbook 21.09.). Frage 1.

### 3.5 Die Soft-Keys

Ausserhalb der Kanalansicht: die eigenen Side-Car-Bänke (Schritt 4), Werksbank 1
Dim · Mono · Speaker B · Talkback. **In** der Kanalansicht: die Schalter der
Seite (Tabelle 3.2). Beim Verlassen kommt die Bank zurück. Frage 2.

---

## 4. Der UF8 (Studie Abschnitt 10)

### 4.1 Die Form

`SelectionMode::TotalMix`, ein neues Mitglied der bestehenden Achse, wie Hue und
DynaMount. Eigenes Builtin, von jeder Fläche auslösbar.

| UF8 | im TotalMix-Modus |
|---|---|
| 8 Strips | 8 sichtbare Kanäle der gewählten Reihe, ◄ ► = 8 weiter |
| 8 Fader | Pegel; bei Input/Playback **der Knoten in den gewählten Submix** |
| 8 V-Pots | Pan in den Submix, oder Gain in der Input-Reihe (einstellbar) |
| Scribble oben / unten | Name (Rolle für Ausgänge, wie auf dem UF1), dB |
| Farbbalken | TotalMix-Farbe über dieselbe `colourMap` wie der UF1 |
| SEL | Kanal wählen: der UF1 zeigt ihn (Fader, STRIP) |
| CUT / SOLO | Mute / Solo |
| Soft-Keys / Quicks | Reihe wählen, Submix wählen, Kontrollraum-Builtins |

⇨ **Das ist der eigentliche Gewinn:** acht Eingänge in `Phones 2` auf acht
Motorfadern, der UF1 daneben mit dem EQ des gewählten Kanals. Ein
Kopfhörermix an der Konsole, ohne TotalMix anzufassen.

### 4.2 Was dafür vorher geändert werden muss

⛔ **Die Auswahl darf nur EINMAL existieren.** Heute stehen Reihe, gewählter
Kanal und Submix als `g_rmeRow` / `g_rmeSel` / `g_rmeSubmix` in `main.cpp`,
und nur der UF1 liest sie. Kommt der UF8 dazu, gehören sie in ein eigenes
Modul (`RmeSurface`, rein, testbar), das beide Flächen fragen: welche Reihe,
welcher Kanal, welcher Submix, welches Fenster von acht. Sonst hat der UF8
seine eigene Kopie, und die zwei laufen auseinander (Fehlerklasse vom 16.09.:
eine Entscheidung, zwei Kopien).

Dasselbe für die Regeln: Namen, Pegel, Adressen, Schritte stehen schon in
`RmeUf1` und sind flächenneutral. Das Modul bekommt einen neutralen Namen.

### 4.3 Kopplung

Wie in der Studie 10.1.1: eine Spalte „UF8 läuft mit" pro Side-Car-Modus,
vorbelegt **an** für RME, **aus** für Item Volume. Betritt der UF1 das
RME-Side-Car, geht der UF8 mit in `SelectionMode::TotalMix`; verlässt er es,
geht der UF8 auf **Norm**, aber nur, wenn er mitgenommen wurde und seither
niemand von Hand umgeschaltet hat. Kein „vorherigen Zustand merken".

Und umgekehrt: den UF8-Modus kann man auch ohne UF1 betreten.

### 4.4 Die sechs Stellen

`g_selectionMode == DynaMount` steht an sechs Stellen verteilt (Studie 10.1).
TotalMix bringt seine eigenen sechs mit. Vor dem Bau die sechs für DynaMount
lesen und sie als Liste in den Bau-Plan schreiben: Painter Scribble, Fader-
Schreibpfad, Fader-Motor, V-Pot, SEL/CUT/SOLO-Tasten, LEDs. Jede davon ist
auch eine Stelle, an der TotalMix sonst REAPER fährt.

---

## 5. REC/RME und das Side-Car

Zwei Wege zu denselben Preamps (Abschnitt 1). Drei Möglichkeiten:

| | was | Folge |
|---|---|---|
| **a** | so lassen | REC/RME braucht weiter TotalReaper, das Side-Car nicht |
| **b** | REC/RME spricht direkt über `RmeManager`, wenn der Link an ist; TotalReaper bleibt der Weg, wenn nicht | ein Preamp-Weg weniger, TotalReaper bleibt für Routing-Mirror und Fader-Sync |
| **c** | REC/RME ganz auf `RmeManager` | TotalReaper wäre für REC unnötig; Routing-Mirror bleibt TotalReapers |

Vorschlag: **a jetzt, b später**, wenn der UF8-Teil steht. Frage 3.

---

## 6. Reihenfolge

1. **Schritt 4 (offen):** eigene Side-Car-Bänke, Builtins Dim / Mono /
   Speaker B / Talkback, Matrix-Schalter in Settings → Bindings → UF1.
2. **STRIP auf dem UF1:** `RmeState` lernt pro Kanal seine Blätter; Seiten aus
   den gemeldeten Blättern; V-Pot-Parameter mit Schritten; Soft-Key-Schalter;
   Graph-Frage 1.
3. **`RmeSurface`:** die Auswahl aus `main.cpp` in ein Modul, beide Flächen
   lesen es. Reine Umstellung, am UF1 nichts Neues.
4. **UF8 `SelectionMode::TotalMix`:** SPREAD mit 8 Strips, Submix-Fader.
5. **Kopplung UF1 ↔ UF8**, Spalte pro Side-Car-Modus.
6. **REC/RME** nach Frage 3.

Jeder Schritt einzeln testbar, jeder mit Tests für die reinen Teile.

---

## 5a. Was RMEs Tabelle sagt (`OSCProtocoll_260721.ods`, gelesen 21.09.)

RMEs Dokument, liegt bei Frank in `~/Downloads`, **nicht ins Repo**.

- **Keine Wertebereiche.** Namen, senden/empfangen, L/R, aber kein Minimum und
  Maximum für Gain, Ratio, Attack, Release, Headroom usw. Offen bleibt also,
  woher die Grenzen kommen: TotalMix-Handbuch (RME, also eine Quelle) oder
  Messung an einem freigegebenen Kanal.
- **Faderkurve exakt** (`CalcFaderDB` / `CalcFaderLin`): jetzt in
  `RmeState.cpp`, trifft alle 21 Messpunkte vom Nachmittag.
- **Schreibbar laut Tabelle:** `stereo`, `pad`, `48v`, `instrument`, `autoset`,
  `phase`, `gain`, `reflevel`, `width`, `msproc`, `fxsend`, alle EQ-, Low-Cut-,
  Dynamics-, AutoLevel-, Room-EQ-Werte, `delay`, `crossfeed`, `loopback`,
  `talkbacksel`. **Nur senden:** `color`, `level`, `status`.
- **L/R:** `phase`, `gain`, `delay` und alle Room-EQ-Bänder gehen auf einem
  Stereokanal pro Seite, rechts = Kanalnummer + 1.
- **`/sendchan/input|playback|output/<n>`** holt alle Werte EINES Kanals. Für
  die Kanalansicht beim Betreten, statt `/sendall`.
- **`/status/device`, `/status/connection`, `/status/dsp`** kommen etwa einmal
  pro Sekunde. Ein Herzschlag: `RmeManager` kann „keine Antwort mehr" daran
  festmachen statt an einem `/sendall` alle 30 s. Noch nicht umgebaut.
- **FX:** `/reverb/...` und `/echo/...` gibt es auch, nicht Teil dieses Plans.
- `reflevel`, `band1type` usw. sind Listenindizes, **geräteabhängig**.

## 5b. Die Grenzen, aus dem UFX+-Handbuch

Quelle: RME, *User's Guide Fireface UFX+*, Kapitel 19.2 und 25.3
(<https://rme-audio.de/downloads/fface_ufxplus_e.pdf>, gelesen 21.09.). Gilt
für die UFX+. Andere RME-Geräte können abweichen, und Listen wie `reflevel`
sind laut OSC-Tabelle geräteabhängig.

| Parameter | Bereich | Schritt | Seite |
|---|---|---|---|
| Gain Mic/Inst (9-12) | 0 dB, dann 8 bis 75 dB (XLR); Inst 8 bis 50 dB | 1 dB | 19.2 |
| Gain Line (1-8) | 0 bis +12 dB | | 61 |
| Ref Level Line-Eingänge | +4 dBu, LoGain | Liste | 61 |
| Ref Level Line-Ausgänge | -10 dBV, +4 dBu, HiGain; 1/2 zusätzlich +24 dBu; Phones Low / High | Liste | 62 |
| Width | -1.00 (vertauscht) bis 1.00 (stereo), 0.00 = mono | | 61 |
| EQ Gain (3 Bänder) | -20 bis +20 dB | 0.5 dB | 62, 64 |
| EQ Freq | 20 Hz bis 20 kHz | | 62 |
| EQ Q | 0.4 bis 9.9 | | 62 |
| EQ Typ Band 1 | Bell, Shelf, High-Pass, Low-Pass; Band 2 nur Bell | Liste | 62 |
| EQ Typ Band 3 | Bell, Shelf, **Low-Pass, High-Pass** (2 und 3 andersherum als Band 1) | Liste | 62 |
| Low Cut Freq | 20 bis 500 Hz | | 62 |
| Low Cut Slope | 6, 12, 18, 24 dB/Okt | Liste | 62 |
| Compressor Threshold | -60 bis 0 dB | | 65 |
| Expander Threshold | -99 bis -20 dB | | 65 |
| Ratio (beide) | 1 bis 10 | | 65 |
| Dynamics Gain (Make-up) | -30 bis +30 dB | | 65 |
| Attack | 0 bis 200 ms | | 65 |
| Release | 100 bis 999 ms | | 65 |
| AutoLevel Max Gain | 0 bis 18 dB | | 65 |
| AutoLevel Headroom | 3 bis 12 dB | | 65 |
| AutoLevel Rise Time | 0.1 bis 9.9 s | | 65 |
| Crossfeed | 5 Stufen, höchstens 6 Stereokanäle | Liste | 62 |
| Delay (Ausgang, im Room-EQ-Fenster) | 0 bis 42 ms | 0.01 ms | 64 |
| Vol.Cal (Ausgang, = OSC `gain` bei Room EQ) | -24 bis +3 dB | 0.1 dB | 64 |
| Room EQ Gain | | 0.1 dB | 64 |

**Nicht im UFX+-Handbuch:** Pad (die UFX+ hat keinen; er kommt bei MADI-Kanälen
mit vorgeschaltetem RME-Preamp, siehe 2.), FX Send als Zahl, die Crossfeed-
Stufen als Werte, die OSC-Werte der Listen (`reflevel`, `crossfeed`).

⇨ **Folge für die Kanalansicht:** die V-Pot-Schritte kommen pro Parameter aus
dieser Tabelle; die Grenzen gelten als UFX+-Werte, und TotalMix klemmt selbst,
wenn ein anderes Gerät engere hat. Listen (Typ, Slope, Ref Level, Crossfeed)
sind Schalter, keine Drehwerte.

## 6a. Entschieden 21.09. (Frank)

- **Graph:** ausserhalb der EQ- und Low-Cut-Seiten **Layout 1 mit den vier
  Farbbalken** in TotalMix-Farben; EQ und Low Cut auf Layout 3 mit Graph (Weg B).
- **Soft-Keys in der Kanalansicht:** die Schalter der Seite.
- **REC/RME:** ⛔ *„Wir müssen das irgendwie zusammenfassen, sonst haben wir am
  Schluss 4 Apps, die alle Ähnliches tun. Aber es darf nicht am UF1-Side-Car
  hängen, sonst wär es ohne UF1 ja nicht nutzbar."* Also: **ein** TotalMix-Weg
  im Kern (`RmeManager` ist schon flächenunabhängig), UF1-Side-Car, UF8, UC1 und
  REC/RME sind nur Nutzer davon. Die Zusammenfassung von TotalReaper, stoerme,
  Rea-Sixty-RME und ORC braucht einen eigenen Plan.
- **UF8-V-Pots in der Input-Reihe: Preamp-Gain.** Pan in den Submix ist genau
  das, was TotalReaper (Hardware-Monitoring für REAPER) schon macht.

## 7. Offene Entscheidungen

1. **Graph nur auf den EQ-Seiten:** Weg A (flache Linie, Layout 3 bleibt) oder
   Weg B (Layout 1 ausserhalb, mit den vier TotalMix-Farbbalken)?
2. **Soft-Keys in der Kanalansicht:** die Schalter der Seite (Vorschlag), oder
   bleiben die Bänke?
3. **REC/RME:** a jetzt, b später?
4. **Grenzen der Parameter:** wo liegt RMEs `OSCProtocoll_260721.ods`? Ohne
   sie messe ich die Grenzen einzeln (wie heute die Faderkurve), was bei Gain
   und Dynamics Pegeländerungen auf einem freigegebenen Kanal bedeutet.
5. ~~**Stereo/Mono per OSC**~~ geklärt 21.09.: TotalReaper schreibt es
   (`TotalReaperCSurf.cpp:1020`). Die Seiten hängen daran, siehe 3.2.
6. **UF8, V-Pots:** Pan oder Gain als Standard in der Input-Reihe?

## 8. Was belegt ist und was nicht

Belegt am 21.09.: Parameterliste pro Streifentyp (Abfrage Remote 3), Pad auf
MADI mit Preamp (TotalReaper-Doku), REC/RME-Aufbau (`main.cpp:1382` ff.,
`recRmeActive_` `main.cpp:1825`), Graph nur in Layout 3 (Runbook), UF8-Achse
`SelectionMode` (Studie 10.1).

Nicht belegt: Grenzen und Schrittweiten der Parameter, ob `stereo` per OSC
schreibbar ist, die sechs DynaMount-Stellen im Einzelnen (in der Studie
gezählt, nicht einzeln aufgelistet).
