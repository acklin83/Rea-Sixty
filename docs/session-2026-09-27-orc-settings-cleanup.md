# Session 27.09.2026 abends: ORC-Einstellungen aufgeräumt, 5-8 schaltet die V-Pot-Bank

Franks Meldung mit vier Bildschirmfotos des ORC-Einstellungsfensters.

## Entscheidungen (Frank, auf Nachfrage)

- ~~5-8 schaltet die Hälfte~~ **ÜBERHOLT, Frank später am Abend:** „soft-key hälften haben
  schon bank < und >. nimm 5-8 für die v-pot bänke". Ich hatte seine Frage „5-8 wieder
  dazuholen" falsch gelesen und die V-Pot-Bänke damit ohne Taste gelassen. Jetzt: 5-8
  schaltet V-Pot-Bank 1/2 wie `24e1825` (22.09.), in STRIP nichts. Lampe gedimmt auf
  Bank 1, hell auf Bank 2, dunkel in STRIP. `RmeInput::button`, `RmeFace::keyLamps`.
- **Farbnamen und Farbfelder aus SSL 360s eigener Liste** (Issue #8, Abschnitt C,
  `LedColourType`). 0x01 heisst jetzt White und zeigt weiss, obwohl der UF8 ihn
  hellblau zeigte.

## Was gebaut wurde

| Teil | Wo |
|---|---|
| 5-8 = V-Pot-Bank in beiden Programmen | `RmeInput::button`, Lampe in `RmeFace::keyLamps` |
| Tab-Leiste ohne Fokusring | `tabs.focusRingType` |
| Alle Hilfesätze raus, keine Rea-Sixty/REAPER-Bezüge mehr im Fenster | `OrcSettingsWindow.mm` |
| Kanalzählung „INPUT 18 PLAYBACK 1 OUTPUT 5“ raus | dito |
| Status als eine Zeile „Status: Online“ statt „TotalMix: online connected to TotalMix (…)“; Menüleiste gleich | `orc::linkSummary` |
| Controls: Spalte „turn“ weg (nur Lautstärke), Bänke als Überschrift, Ziele als Wörter („Phones 1“, Kanäle unter Inputs/Playback/Outputs), Push als „Submix/Select/Mute/Nothing“, dB als Einheit hinter dem Feld | dito |
| Soft Keys: Aktionen ohne „RME: “, Tastennummern 5-8 auf der zweiten Hälfte | dito |
| Colours: SSL-Namen, SSL-Farbfelder, keine Hex-Zahlen, nur die 13 Einträge, die leuchten (vorher dreimal „off“ zusätzlich), TotalMix-Farben gross geschrieben ohne OSC-Nummer | `Palette.cpp` (`paletteName`, neu `paletteSwatch`) |
| Grids nicht mehr auf Fensterbreite gestreckt (Lücke zwischen „key“ und „label“) | `pageWithStack` |
| Port-Meldung ohne „stoerme, TotalReaper or a second REAPER?“ | `RmeManager.cpp` |

`paletteName` hat nur ORC als Leser (gegreppt). Der Quantisierer (`paletteEntry`,
`kPalette`) ist unverändert.

## Geprüft

- ctest 17/17. Der neue 5-8-Test fällt mit ausgeschaltetem Zweig um (7 Fehler).
- Alle vier Seiten offscreen gerendert (Prüfprogramm mit den ORC-Objekten, Kopie von
  `orc.json`, ohne TotalMix-Verbindung) und angesehen. Dabei zwei Layoutfehler
  gefunden und behoben (Streckung, gequetschtes Host-Feld).
- NICHT gesehen: die Tab-Leiste selbst (fehlt im Offscreen-Bild) und die Zielmenüs mit
  Kanal-Überschriften (brauchen eine TotalMix-Verbindung). 5-8 am Gerät ungesehen.

## Offen

- ORC-Menü „UF1 not open: handed over to REAPER“ nennt REAPER. Nur sichtbar, wenn
  Rea-Sixty den UF1 übernimmt; nicht angefasst.

## Nachtrag: Control Room in TotalMix' Reihenfolge

Frank: die Control-Room-Ausgänge hüpften beim Kanal-Encoder und Nav ◄ ► durcheinander
(Phones 1, Phones 2, Main, Main B, Phones 3 …), weil `visibleChannels` nach RMEs
Kanalnummer sortierte, und `followBank` die V-Pot-Bank jedes Mal mitnahm.
In TotalMix nachgesehen (Fenster, 27.09.): der Control Room steht rechts als eigener
Block, **Phones 1, Phones 2, Phones 3, Phones 4, Speaker B, Main**.

- `RmeUf1::orderKey`: übrige Ausgänge nach Nummer, dann die Rollen aus
  `/controlroom/phones1..4`, `mainoutb`, `mainout` in dieser Reihenfolge.
  `visibleChannels` sortiert danach (Eingänge, Playbacks unverändert).
- `stepChannel` bekommt den Schlüssel mit: von einem inzwischen versteckten Kanal
  suchte es per `upper_bound` in Nummernordnung.
- Leser von `visibleChannels` geprüft: Kanal-Encoder, Nav ◄ ► (Submix), `selected`,
  `effectiveSubmix` (Rückfall ohne Main ist jetzt der erste gezeigte statt die
  kleinste Nummer), ORC-Zielmenü, ORC-Startausgabe. Alle wollen dieselbe Reihenfolge.
- `test_rme_input` hielt die alte Nummernordnung fest (Main vor Phones 1), angepasst.
  Neuer Test in `test_rme_osc`, fällt ohne die Sortierung um.
- Danach Frank: **Control Room zuerst**, dann die übrigen Ausgänge
  (`kControlRoomKey` negativ). Und Bank 2 ab Werk **Main B, Main** (TotalMix'
  Reihenfolge), `RmeManager.h`. Franks eigene `rme.json` bei gestopptem ORC ebenso
  gedreht (Zeilen 10/11 getauscht, Rest unverändert).

## Reverb und Echo über OSC

Frage von Frank. Gemessen im Mitschnitt `/tmp/rme_osc_trace.log` (26.09., Antwort auf
`/sendall`): TotalMix SENDET
- `/reverb/` enable, type, predelay, lowcut, highcut, attack, hold, release, roomscale,
  time, highdamp, smooth, volume, width
- `/echo/` enable, type, delay, feedback, highcut, volume, width
- pro Eingang `/input/<n>/fxsend` (dB), pro Ausgang `/output/<n>/fxreturn` (dB),
  `/controlroom/mutefx`. Kein `/playback/<n>/fxsend` im Mitschnitt.
Nicht geprüft: ob TotalMix auf dieselben Adressen SCHREIBEND reagiert (wir haben nie
eine gesendet), und was die Zahlen bei `type` bedeuten. `RmeState` speichert davon heute
nichts.

### Die Auswahllisten (Franks Bildschirmfotos aus TotalMix, 27.09.)

Der OSC-Wert ist die Position im Menü, von 0 an. Belegt an den gemessenen Werten:
reverb type 2 = Large Room, echo type 0 = Stereo Echo, echo highcut 0 = off (Mitschnitt
27.09.); der Mitschnitt vom 26.09. hatte highcut 3, das wäre 8k. Die übrigen Positionen
folgen der Menüreihenfolge, einzeln gemessen sind sie nicht.

| Wert | reverb type | echo type | echo highcut |
|---|---|---|---|
| 0 | Small Room | Stereo Echo | off |
| 1 | Medium Room | Stereo Cross | 16k |
| 2 | Large Room | Pong Echo | 12k |
| 3 | Walls | | 8k |
| 4 | Shorty | | 4k |
| 5 | Attack | | 2k |
| 6 | Swagger | | |
| 7 | Old School | | |
| 8 | Echoistic | | |
| 9 | 8plus9 | | |
| 10 | Grand Wide | | |
| 11 | Thicker | | |
| 12 | Envelope | | |
| 13 | Gated | | |
| 14 | Space | | |

Vermutung, ungeprüft: attack/hold/release gehören zu Envelope und Gated, time/highdamp
zu Space (die drei Typen am Ende der Liste, die andere Regler brauchen).

### Schreiben geprüft (19:34, mit Franks Okay)

`/echo/enable 1`, drei Sekunden später `/echo/enable 0`, als UDP an 127.0.0.1:7005 (ORCs
Remote-Port bei TotalMix). Frank sah es in TotalMix. Der Mitschnitt von ORC zeigt keine
Rückmeldung: TotalMix meldet einer Remote nicht, was an ihren eigenen Port geschrieben
wurde. Ein Side-Car, der Reverb/Echo schreibt, muss den Wert selbst übernehmen.
Playback-FX-Send danach auch probiert (19:38, mit Franks Okay): `/playback/0/fxsend -10`,
fünf Sekunden später `-300` (aus), auf AN 1/2. Frank sah es. **Schreibbar, aber TotalMix
meldet ihn nie**: der Side-Car kann ihn setzen, aber den aktuellen Wert nicht kennen.
