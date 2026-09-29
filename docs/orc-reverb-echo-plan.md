# Plan: Reverb und Echo auf der UF1 (Side-Car und ORC)

Stand 29.09.2026. Nur Plan, nichts gebaut. Gilt für beide Programme, weil ORC den
Side-Car der Extension wortwörtlich fährt (RmeState, RmeUf1, RmeStrip, RmeInput,
RmeFace, RmeBuiltins).

## Franks Entscheidungen (29.09.)

1. **Eine eigene Reihe FX unter den Outputs**, per Nav-Kreuz erreichbar. Keine FX-Seiten
   auf jedem Kanal.
2. Typabhängige Regler **nur beim passenden Reverb-Typ**.
3. **FX Return pro Output**, im STRIP direkt nach Pan, Xfeed, Delay, Ref Lvl.
4. **Mute FX nur im Picker**, auf keiner Werkstaste.
5. Kurzformen wie vorgeschlagen, Frank korrigiert am Gerät.

## Was es schon gibt

**Gemessen** (`docs/session-2026-09-27-orc-settings-cleanup.md`, Mitschnitte 26./27.09.):
- TotalMix sendet auf `/sendall` 14 Reverb-Felder (`enable type predelay lowcut highcut
  attack hold release roomscale time highdamp smooth volume width`), 7 Echo-Felder
  (`enable type delay feedback highcut volume width`), `/input/<n>/fxsend`,
  `/output/<n>/fxreturn`, `/controlroom/mutefx`.
- Werte in echten Einheiten: predelay 20 (ms), lowcut 20, highcut 18000 (Hz), roomscale
  1.0, time 3.0 (s), smooth 100, volume -3 (dB), width 0.6, echo delay 0.6 (s), feedback 20.
- Menüwerte = Position im TotalMix-Menü ab 0 (15 Reverb-Typen, 3 Echo-Typen, 6
  Echo-High-Cut-Stufen, Tabelle in der Session-Notiz).
- Schreiben: `/echo/enable` geht (Frank sah es). TotalMix meldet der schreibenden Remote
  nichts zurück; `Manager::send` spielt jeden eigenen Schreibvorgang sofort in den eigenen
  Zustand (`localEcho`, `RmeManager.cpp:323`), das deckt es ab.

**RMEs OSC-Tabelle** (`OSCProtocoll_260721.ods`, 2.1 beta 2) nennt nur `/reverb/enable`,
`/echo/enable`, `fxsend`, `fxreturn`, `/mutefx`. Die übrigen Felder sendet TotalMix, ob es
sie auch annimmt, ist nicht geprüft.

**RMEs UFX+-Handbuch, Kap. 25.6** (rme-audio.de/downloads/fface_ufxplus_e.pdf, gelesen 29.09.):

| Feld | Bereich laut Handbuch | Gilt für |
|---|---|---|
| PreDelay | 0 bis 999 ms | alle Typen |
| Low Cut | 20 bis 500 Hz | alle |
| High Cut | 5 bis 20 kHz | alle |
| Smooth | 0 bis 100 | alle |
| Width | 100 (stereo) bis 0 (mono); OSC meldet 0.6, also 0..1 | alle |
| Volume | nicht angegeben | alle |
| Room Scale | 0.5 bis 3.0 | „Room Types" |
| Attack, Hold, Release | je 5 bis 400 ms | Envelope, Gated |
| Reverb Time | 0.1 bis 4.9 s | Space |
| High Damp | 5 bis 20 kHz | Space |
| Echo Delay Time, Feedback, Volume | nicht angegeben | Echo |
| Echo Width | 100 bis 0, OSC 0..1 | Echo |

**Im Code:** `fxsend` steht auf der Werksseite „Input". `fxreturn` steht im Katalog, auf
keiner Seite. `mutefx` wird gelesen (`State::muteFx`), nichts benutzt es.

## Die FX-Reihe

Vier Reihen statt drei: Input, Playback, Output, **FX**. Nav auf/ab und MODE + Kanal-Encoder
laufen durch alle vier, mit Umlauf wie heute. Die FX-Reihe hat zwei Kanäle, **Reverb** und
**Echo**, in dieser Reihenfolge. Sie sind da, sobald TotalMix ihre Felder gemeldet hat.
Eine leere FX-Reihe verhält sich wie eine leere Playback-Reihe heute.

Was jedes Bedienelement auf der FX-Reihe tut:

| Element | Input/Playback/Output heute | FX-Reihe |
|---|---|---|
| Kanal-Encoder | Kanal der Reihe | Reverb / Echo |
| Fader | Pegel (Knoten im Submix, Output selbst) | Volume des Effekts (`/reverb/volume`, `/echo/volume`, dB) |
| V-Pot über dem Fader | Pan | Width |
| Druck darauf | Pan Mitte | Width auf 1.0 (stereo) |
| CUT | Mute | Effekt aus (Lampe an = aus, wie Mute) |
| SOLO | Solo in den Submix | nichts, Lampe dunkel |
| SEL | Output wird Submix | nichts |
| Soft-Key über dem Kanal | Stereo/Mono | nichts |
| Kanal-Druck | STRIP auf/zu | STRIP auf/zu, mit den Reverb- bzw. Echo-Seiten |
| Kanalzone (SPREAD) | Name, Farbbalken, Wert, Pegel | Name, Farbbalken, Volume. **Kein Pegelbalken**: TotalMix meldet für die Effekte keinen Pegel |
| Nav ◄ ► | Submix | Submix wie überall (ändert an FX nichts) |
| MASTER, 5-8, V-Pots 1-4 | wie heute | wie heute |

Farbe: fest weiss (Palette 1), TotalMix meldet für die Effekte keine Farbe.

Volume reicht wie jeder TotalMix-Fader von -65 bis +6 dB (gemessen 29.09.). Der Fader bildet
seine Stellung mit TotalMix' Fadergesetz auf dB ab (`faderlinToDb`) und schreibt dB.

### Die STRIP-Seiten der FX-Reihe

Neue Zeilenmarke `fx` für `StripPage::rows`. Die Seiten eines Kanals werden wie heute
gepackt (gleiche `rows` nebeneinander = ein Vorrat, vier pro Ansicht). Was ein Kanal nicht
hat, fällt heraus: Echo meldet kein `predelay`, also erscheinen Reverb-Regler dort nicht.
Gleich benannte Felder (`type`, `highcut`, `width`, `volume`, `enable`) bekommen im Katalog
getrennte Einträge mit der Bedingung „nur Reverb" bzw. „nur Echo".

| Kanal | Pots (in dieser Reihenfolge, gepackt) | Tasten |
|---|---|---|
| Reverb | Rev Type, PreDelay, RoomScl¹ oder Time⁴, Low Cut, High Cut² oder HiDamp⁴, Smooth, Attack³, Hold³ ⁵, Release³ ⁵ | Reverb (an/aus) |
| Echo | EchoType, Delay, Feedback, HiCut | Echo (an/aus) |

Volume und Width stehen nicht im STRIP, sie liegen auf Fader und Pan-Pot.
Reihenfolge wie im FX-Fenster von TotalMix. Sichtbar nach Typ (Entscheidung 2, gemessen unten):
¹ alle ausser Envelope, Gated, Space · ² alle ausser Space · ³ Envelope · ⁴ Space · ⁵ Gated

### FX Return pro Output

Neue Werksseite „Output 2" mit `rows = "out"`, Pot 1 = `fxreturn`, direkt nach „Output".
Weil beide `out` tragen, packt der STRIP sie zu einem Vorrat: Pan, Xfeed, Delay, Ref Lvl,
dann FX Ret auf der nächsten Ansicht.

### Mute FX

Built-in `rme_mute_fx` über `regRmeCr` (wie Dim), Lampe aus `State::muteFx`, umschaltend
(`BuiltinKind::Switch`), Picker-Kategorie wie die anderen RME-Built-ins. Keine Werkstaste.

### Anzeige (8 Zeichen)

- Reverb-Typen: `Small`, `Medium`, `Large`, `Walls`, `Shorty`, `Attack`, `Swagger`,
  `OldSchl`, `Echoist`, `8plus9`, `GrandWd`, `Thicker`, `Envelope`, `Gated`, `Space`
- Echo-Typen: `Stereo`, `Cross`, `Pong`
- Echo High Cut: `off`, `16k`, `12k`, `8k`, `4k`, `2k`
- Reihe: `FX` im Reihenkopf und in der MODE-Liste

## ⛔ Die Falle: „Output oder der Rest"

Viele Funktionen kennen heute zwei Fälle, Output und „sonst Input/Playback". Eine vierte
Reihe landet dort still im falschen Zweig. Jede Stelle bekommt einen FX-Zweig und einen Test:

| Stelle | heute | ohne FX-Zweig |
|---|---|---|
| `RmeUf1.cpp` `mapOf`, `channelOf` | switch über drei Reihen | findet nichts oder die Output-Map |
| `RmeUf1.cpp` `displayName`, `orderKey`, `visibleChannels` | `colour != 0` Pflicht | FX unsichtbar (keine Farbe gemeldet) |
| `RmeUf1.cpp` `levelDb`, `levelAddress` | Output sonst Submix-Knoten | schreibt `/mix/...` |
| `RmeUf1.cpp` `muteAddress` | `/output/` sonst `/input/`, `/playback/` | schreibt auf einen Playback |
| `RmeUf1.cpp` `panAddress`, `panValue`, `soloAddress`, `soloed`, `eqModel` | Output sonst Submix | falsche Adresse |
| `RmeFace.cpp:685` Pegelquelle | Output, Input, sonst `levelPb` | zeigt Playback-Pegel |
| `RmeFace.cpp:289` `/sendchan` beim STRIP-Öffnen | pro Kanal | `/sendchan/fx/0` gibt es nicht; für FX nichts senden |
| `RmeFace.cpp:304` Fader `writable` | Output oder Submix da | FX braucht keinen Submix |
| `RmeStrip.cpp` `section`, `addr`, `available`, `value`, `sendChanAddress` | `/<section>/<n>/<leaf>` | FX: `/reverb/<leaf>` bzw. `/echo/<leaf>` |
| **9×** `std::clamp(s.row.load(), 0, 2)` (`RmeInput.cpp` 164, 193, 212, 240, 300, 310, 348, 389; `RmeFace.cpp` 247) | feste 2 | FX nie erreichbar; wird `kRowCount - 1` |
| `RmeFace.cpp:644` MODE-Liste `vis[3]` | drei Einträge | vier |

**Wer über `kRowCount` schleift, meint zweierlei.** Navigation will vier Reihen, alles
über TotalMix-Kanäle will drei. Neu: `kMixerRowCount = 3` neben `kRowCount = 4`.

| Leser | braucht |
|---|---|
| `RmeInput.cpp:95` `stepRow` | 4 |
| `OrcSettingsWindow.mm:62` Zielmenü (V-Pots, Jog) | 3. ⛔ `kinds[]` und `headers[]` haben drei Einträge, mit 4 läse die Schleife über das Array hinaus |
| `OrcSettingsWindow.mm:614` Farbträger | 3 |
| `orc/Surface.cpp:80` Startausgabe | 3 (eigene Liste) |

## Dateien

| Datei | Änderung |
|---|---|
| `RmeState.h/.cpp` | `State::fx`: zwei `Channel` (Reverb 0, Echo 1) mit `leaves`, `name`, `seen`. `ingest`: `/reverb/<leaf>`, `/echo/<leaf>` |
| `RmeUf1.h/.cpp` | `Row::Fx = 3`, `kRowCount = 4`, `kMixerRowCount = 3`, `rowName` „FX", FX-Zweig in allen Funktionen der Tabelle oben |
| `RmeStrip.h/.cpp` | Zeilenmarke `fx`, `Need::Reverb`/`Echo` und die Typbedingungen, neue Katalogeinträge, zwei neue Arten (Prozent: Smooth, Feedback; Faktor: Room Scale), Adressierung `/reverb/`, `/echo/`, Push-Standard (siehe unten). Nach einem geschriebenen Reverb-Typ zusätzlich `/sendall`, weil der Typ Werte lädt (gemessen). Wechselt der Typ, ändert sich der Vorrat der Ansichten; die offene Ansicht wird wie heute über ihre `id` gesucht, sonst die erste |
| `RmeInput.cpp` | Klemmen auf `kRowCount - 1`, FX-Zweige für CUT, Pan-Pot (Width), SOLO/SEL/Soft-Key nichts |
| `RmeFace.cpp` | Klemme, Pegelquelle, `/sendchan`, Fader, MODE-Liste, Kanalzone ohne Pegel |
| `RmeManager.cpp` | Werksseiten „Output 2", „Reverb", „Echo". `rme.json` **Version 5**: Eine vorhandene `strip`-Liste ersetzt die Werksseiten heute ganz (`RmeManager.cpp:177-198`), sonst kämen die Seiten bei niemandem an, auch nicht in Franks zwei Dateien (`ORC/rme.json`, `REAPER/rea_sixty/rme.json`). Die Hochstufung setzt „Output 2" direkt hinter eine Seite mit `rows = "out"` und hängt die FX-Seiten hinten an, jeweils nur wenn sie fehlen |
| `RmeBuiltins.cpp` | `rme_mute_fx`, `Manager::ControlRoom::muteFx` |
| `OrcSettingsWindow.mm`, `orc/Surface.cpp` | Schleifen auf `kMixerRowCount` |
| Picker-Kategorie + `check_builtin_docs.py` | Pflicht für `rme_mute_fx` |
| `tests/test_rme_osc.cpp`, `test_rme_input.cpp`, `test_rme_face.cpp` | ingest; jede Stelle der Fallen-Tabelle; Reihenumlauf über vier; Typbedingungen; Hochstufung v4 auf v5 genau einmal und Reihenfolge Output/Output 2 |
| `docs/user-manual.md` | Side-Car-Kapitel: FX-Reihe, Output 2, Mute FX |
| Changelog | Eine Zeile |

Push auf einem FX-Pot: Standardwerte, wie sie im Mitschnitt stehen, sind TotalMix' eigene
Voreinstellung nicht sicher. Vorschlag: nur Width auf 1.0, sonst nichts (wie Ref Level heute).

**Wer fasst die Werte sonst noch an:** `State::fx` schreibt nur `ingest` (TotalMix und
`localEcho`); lesen STRIP, Fader, Kanalzone, CUT-Lampe. `State::muteFx` schreibt `ingest`,
liest nur das neue Built-in. `InputState::row` schreiben `stepRow`, `select`, `faderMainFire`;
alle Leser stehen in der Fallen-Tabelle.

## Gemessen 29.09.2026 (TotalMix 2.10 alpha 8, UFX+)

Über Remote 1 (TotalMix hört auf 7001, antwortet an 7002; niemand sonst hielt 7002).
Geschrieben mit `/<feld> <wert>`, zurückgelesen mit `/sendall`. Reverb und Echo waren
dabei aus, TotalMix steht danach wieder Wert für Wert auf dem Ausgangsstand (Rückvergleich).

**1. TotalMix nimmt alle 17 Felder an.** Jeder geschriebene Wert kam exakt zurück.
Die OSC-Tabelle nennt nur `enable`, TotalMix nimmt trotzdem alle.

**2. Grenzen**, gemessen durch Schreiben von ±100000 und Zurücklesen, bei festem Typ:

| Feld | min | max | Handbuch |
|---|---|---|---|
| reverb predelay | 0 | 999 ms | gleich |
| reverb lowcut | 20 | 500 Hz | gleich |
| reverb highcut | **2000** | 20000 Hz | 5 kHz min |
| reverb smooth | 0 | 100 | gleich |
| reverb width, echo width | 0 | 1 | 100..0 |
| reverb volume, echo volume | -65 | +6 dB | nicht angegeben |
| reverb roomscale | 0.5 | 3.0 | gleich |
| reverb attack, hold | 5 | 400 ms | gleich |
| reverb release | 5 | **500** ms | 400 |
| reverb time | 0.1 | **5.0** s | 4.9 |
| reverb highdamp | **2000** | 20000 Hz | 5 kHz min |
| echo delay | 0.1 | 2.0 s | nicht angegeben |
| echo feedback | 0 | 100 | nicht angegeben |
| echo highcut | 0 | 5 (Liste) | |
| reverb type | 0 | 14; **15 und -1 werden 0** (kein Klemmen) | 15 Typen |
| echo type | 0 | 2, geklemmt | 3 Typen |

Wo Handbuch und TotalMix abweichen, gilt TotalMix.

**⛔ Ein Typwechsel lädt Werte des Typs.** Reverb-Typ auf Gated verstellte `highcut`
(10000 auf 8000) und `roomscale` (2.0 auf 1.5). TotalMix meldet das der schreibenden Remote
nicht. Also: nach jedem geschriebenen Typ den Stand neu holen (`/sendall`), sonst zeigt der
STRIP die alten Werte. Gilt für Reverb; ob der Echo-Typ auch Werte lädt, war nicht zu sehen
(die Echo-Felder blieben gleich).

**Beim Schreiben des Typs** nicht über 14 bzw. 2 hinaus: TotalMix springt dann auf 0.
Der Katalog klemmt ohnehin auf die Liste.

**3. Regler pro Typ** (Franks Bildschirmfotos des FX-Fensters, 29.09.):

| Typ | Pots neben Pre Delay, Low Cut, Smooth, Width, Volume |
|---|---|
| Large Room | Room Scale, High Cut |
| Shorty | Room Scale, High Cut |
| Envelope | High Cut, **Attack, Hold, Release** |
| Gated | High Cut, **Hold, Release** (kein Attack) |
| Space | **Time, High Damp** (kein High Cut, kein Room Scale) |

Das Handbuch liegt bei Gated falsch: Es nennt Attack auch für Gated, TotalMix zeigt ihn nicht.
Gemessen sind 5 der 15 Typen. Small Room, Medium Room, Walls und die sieben übrigen
(Attack, Swagger, Old School, Echoistic, 8plus9, Grand Wide, Thicker) sind als „wie Large
Room und Shorty" angenommen, nicht gesehen.

TotalMix zeigt Width als `+0.60`, Smooth als `100%`, Room Scale als `1.00`, Volume als `-3.0`.
Die Anzeige auf der UF1 folgt dem.

## Was ich nicht baue

- Playback-FX-Send (TotalMix meldet ihn nie).
- Echo in BPM (TotalMix sendet nur Sekunden).
- Presets von TotalMix.
- FX als V-Pot- oder Jog-Ziel in `rme.json`.
- Reverb/Echo auf dem UF8.
