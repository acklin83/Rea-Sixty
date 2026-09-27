# Session 27.09.2026: Side-Car-Lampen, Bankhälften auf Bank ◄ ►, gepackte STRIP-Seiten

Commit `7f4bf85`. Deployt `9f94a7ec` (REAPER lief nicht), ORC aus
`extension/build/ORC.app` neu gestartet.

## Franks Fragen und Entscheidungen

- „Wieso 5-8 für die Hälften?“ Das war sein Wortlaut vom 25.09. 19:24
  („bank 2: snapshots mit 5-8“). Bank ◄ ► waren seither im Side-Car frei.
  **Entschieden:** Bank ◄ = Tasten 1-4, Bank ► = 5-8. 5-8 tut im Side-Car nichts.
- „LEDs mit Funktion leuchten lassen“: **Drei Zustände wie am UF8-Panel**,
  gedimmt = Funktion, hell = Zustand an, dunkel = nichts. Weiss.
  **Nav hoch/runter/links/rechts gedimmt** (vorher immer hell).
- STRIP-Seiten verschwenderisch (T2: Seite 1 nur Phase + FX Send, Seite 2 nur
  Stereo). **Entschieden: A, Nachrücken.**

## Was gebaut wurde

| Teil | Wo |
|---|---|
| Lampen-Entscheidung, rein, für beide Programme | `RmeFace::keyLamps` (18 Tasten), `LampFacts` |
| Lampen-Sender, dieselben Bytes wie REAPERs Tastendurchgang, Nav-Kreuz alle 500 ms | `RmeFace::emitLamps` |
| Wer welche Lampe malt | Side-Car: alles ausser `RmeInput::passesThrough` (SHIFT, Transport, CYCLE, CLICK, 360). Die malt der Wirt |
| Rea-Sixty: Tastendurchgang und Nav-Kreuz lassen die Side-Car-Tasten in Ruhe; Nav-Cache wird beim Verlassen geleert | `uf1PaintButtonLeds_`, `uf1NavCrossSyncLeds_` |
| ORC: durchgelassene Tasten aus `orc.json`, belegt gedimmt, aktiv hell | `orc/Surface.cpp`, `host.buttonLeds` |
| Bank ◄ ► wählen die Hälfte, < > die Bank, eine Funktion für beide | `RmeSoftKeys::stepKey`, `pickHalf`, `hasSecondHalf` |
| Builtin `uf1_dyn_bank_page` im Side-Car: +1 Hälfte 2, -1 Hälfte 1 | `main.cpp` |
| STRIP: benachbarte Seiten mit gleichen `rows` werden pro Kanal gepackt | `RmeStrip::views`, `RmeInput::stripView` (ersetzt `stripPage`/`stripParam`/`availablePages`) |
| ORC Soft-Keys-Tab: „Keys 5-8“, Satz zu BANK left/right | `OrcSettingsWindow.mm` |

## Regeln im Code

- **Bank ◄ ► Lampe:** wie < >, hell in die Richtung, in der es noch eine Hälfte
  gibt, sonst dunkel (`39638bb`; die erste Fassung zeigte die gezeigte Hälfte,
  Frank: „genau falsch herum“). Ohne zweite Hälfte (statische Bank, leerer
  Shift-Satz) beide dunkel und wirkungslos, in STRIP auch.
- **SHIFT in ORC** (`74764ae`): `mod_shift`/`mod_cmd`/`mod_ctrl` samt Latch liegen
  jetzt in `Bindings.cpp` (`registerModifierBuiltins`), ORC registriert sie.
- **< >:** hell, solange es weitergeht, am Ende dunkel (wie vorher).
- **Nav-Mitte:** ohne TotalMix-Verbindung dunkel.
- **MASTER:** ohne Main dunkel. **Stereo-Taste:** ohne Kanal dunkel.
- **Packen:** nur Läufe von ZWEI oder mehr benachbarten Seiten mit gleichem,
  nicht leerem `rows` (Werksseiten: Input + Input 2). Eine Einzelseite (Output,
  EQ, Dyn …) bleibt platzgetreu. View-Id = Seite × 8 + Nummer im Lauf, bleibt
  beim Kanalwechsel stehen.

## Tests

`test_rme_face` (Lampen-Tabelle, keine durchgelassene Taste in der Liste,
Bytes), `test_rme_osc` (T2 eine Seite, fünfte Taste öffnet Input 2, Einzelseite
platzgetreu, Stereo), `test_rme_softkeys` (Bank ◄ ► wählt, 5-8 geschluckt,
< > ohne Umlauf, keine zweite Hälfte, STRIP). Gegen zwei absichtliche Brüche
geprüft (Nav hell, Packen aus). ctest 17/17.

## Meter-Forum: der 360-Core-Empfänger läuft jetzt immer

Drei Berichte (zwei Win 11, einer macOS Tahoe): Goniometer und RTA leer, Pegel,
Infofeld und VU laufen. Ursache: der Impersonator war opt-in über ExtState
`rea_sixty/ssl_core`, den keine Settings-Seite, kein Installer und nicht das
Handbuch setzt. Frank hat ihn seit Juli an, darum ging es bei ihm überall.
Frank: kein Schalter, jeder Nutzer hat SSL 360 zu, das ist unsere Bedingung.
Jetzt startet er immer (`main.cpp`, Tor und ExtState entfernt), README
nachgezogen. **Gehört in die Notes der nächsten Version.** Der Mixbus-Fall
(„gar nichts“) ist damit nicht sicher erklärt; der Ersatzweg las die fokussierte
Spur. Nach dem Update nochmal fragen.

## Suchfelder bleiben stehen (`041ca00`)

- Settings, FX Learn: die Parameterzeilen haben ein eigenes Child
  (`fxl_param_rows`) unter Hinweis und Filter, nur sie scrollen.
- Learn-HUD: die Zeilen werden auf ihr Band beschnitten
  (`ImGui_DrawList_PushClipRect`, Signatur aus der installierten ReaImGui gelesen).
  Vorher lag eine halb sichtbare Zeile über der Filterzeile, samt Klickfläche.
- Am Bildschirm nicht angesehen (REAPER lief nicht).

## Automation auf UF1 und UF8 (abends, von Frank am Gerät bestätigt: „jetzt passt es“)

| Was | Wie es jetzt ist | Commit |
|---|---|---|
| UF1 folgt Automation | Motor, dB, Pan, DAW-Ansicht lesen `GetTrackUIVolPan` (uiVolLinear / uiPan) statt `D_VOL`/`D_PAN` | `5f03f16` |
| UF1-Fader in Touch | `GetTouchState` meldet den UF1-Fader (g_uf1FaderVolTr) | `5f03f16` |
| FLIP-Pan auf Fadern (UF8, UF1) | `CSurf_OnPanChange` + Berührung pro Fader-Platz (g_faderPanTr) | `7f66897` |
| Plug-in-Parameter auf Fadern | `TrackFX_EndParamEdit` beim Loslassen (noteFaderFxEdit_/endFaderFxEdit_) | `b866e3a` |
| Pan/Lautstärke/Sends auf Pots in Touch | eigener Hüllkurvenpunkt an der hörbaren Position (potEnvelopeWrite_), keine Berührung, `UpdateArrange`, ein Undo pro Bewegung; sonst alter Weg (legt fehlende Hüllkurve an) | `ce1419f`, `302c172` |
| Fader melden nur, was sie schreiben | g_faderVolTr / g_faderPanTr, beim Loslassen gelöscht | `302c172` |

Sackgassen, nicht wiederholen:
- Pots „halten bis Stop“ (`a40891b`): Pan schrieb weiter, hielt spätere FLIP-Fader fest. Zurück `8402e99`.
- Pots ohne Berührung, nur absolut schreiben (`f315cbb`): Zickzack. Die Juni-Notiz stimmte. Zurück `b4a90e7`, Warnung im Code.


## Offen

- **Frank am Gerät:** alles oben. Gedimmtes Weiss auf dem Nav-Kreuz war vorher
  nie an; dieselben Bytes wie ein gedimmter gebundener Knopf (SHIFT im Leerlauf).
- Welche Parameter TotalMix auf einem Mic-Kanal meldet, ist nicht gemessen; der
  Test benutzt einen erfundenen Satz.
