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

## Offen

- **Frank am Gerät:** alles oben. Gedimmtes Weiss auf dem Nav-Kreuz war vorher
  nie an; dieselben Bytes wie ein gedimmter gebundener Knopf (SHIFT im Leerlauf).
- Welche Parameter TotalMix auf einem Mic-Kanal meldet, ist nicht gemessen; der
  Test benutzt einen erfundenen Satz.
