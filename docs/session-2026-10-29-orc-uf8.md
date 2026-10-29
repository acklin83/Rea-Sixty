# Session 29.10.2026: ORC für den UF8, Plan und Stufen 1 bis 4

## Plan
`docs/orc-uf8-plan.md`, dritte Fassung, von Frank freigegeben („so bauen“). Kern: der UF8
bekommt Side-Car-Modi wie der UF1 (TotalMix, Hue, DynaMount), gleich in ORC und Rea-Sixty.
OBS ist kein Modus, sondern Soft-Keys. REC/RME über TotalReaper bleibt. Jedes Programm hat
seine eigenen Einstellungen (Import-Knopf statt gemeinsamem Ordner, der bei Installations-
reihenfolge, Versionsschiefe, Windows-MSIX und portablem REAPER bricht).

## Gebaut
- **Stufe 1 `c2dfbd21`** Übergabe mit Quittung: ORC schreibt `orc.pid` und
  `handover.released`, Rea-Sixty schreibt die Marke mit `v2`-Zeile und öffnet UF1/UF8 erst
  nach der Quittung (höchstens 3 s). UF8-Retry auch, wenn REAPER den UF8 noch nie hatte, ORC
  ihn aber hält. Format in `src/OrcHandover.h`, Test `test_orc_handover`.
- **Stufe 2 `52ec0e7d`** `src/Uf8Frames.h`: UF8-Framing wortwörtlich aus `onUf8InputUnit_`
  (Präfix `31 xx`, geteilte Frames, Prüfsumme). `surfsleep::uf8Frames`. Tests
  `test_uf8_frames`, `test_surface_sleep`. Einzige Verhaltensänderung: nach einem Weckdruck
  wartet ein abgeschnittener Frame-Rest im Puffer, statt verworfen zu werden.
- **Stufe 3a `2ca11262`** Hue und DynaMount als UF8-Side-Car: ganzer linker UF8 (übrige
  Strips dunkel, ein rechter UF8 zeigt weiter Spuren), dieselbe Taste führt in den vorigen
  REAPER-Modus zurück, Shift + LAYER 2 / 3 ab Werk und per Bindungen v56 (nur leere Plätze).
  Test `test_sidecar_keys`. Handbuch.
- **Stufe 4 `bfdd0544`** DynaMount-Presets: acht Szenen aller Stative, über IP, Bankart
  `DynaMountPresets` mit den Gesten der Selection Sets, Builtins
  `dynamount_preset_recall/_store`, Tabelle im Dynamount-Reiter, ExtState
  `dynamount_presets`. Tests in `test_dynamount`. Handbuch.

- **Stufe 5** `4298de10` `Uf8Fader.h`, `b825a7ac` Ring-Frames in `Protocol`, `8bd767f0`
  `Uf8Strips.h` (der MIDI-CC-Layer malt darauf), `fb65e371` V-Pot-Delta und dunkle Einheit,
  `b6d18035` `Uf8Rme` (TotalMix auf 8 Strips, Kanalansicht, Auswahl über `input::State`),
  `772198d4` ORC fährt den UF8. Tests `test_uf8_fader`, `test_uf8_strips`, `test_uf8_rme`.
- **Stufe 6** `dded8af3` `Uf8ModeViews.h` (was ein Stativ / eine Lampe auf dem Strip zeigt),
  `12154248` `ModeBuiltins` (Hue/DynaMount/OBS-Aktionen, Aufnahmelicht nur in Rea-Sixty),
  `367b5b76` `ModeSettings` (ein Lader, Import aus ORC in Rea-Sixty), `e73fb5e4` ORC behält
  Hue/OBS/DynaMount in `<ORC>/modes`, Standpositionen reisen in `handover.released` /
  `handover.returned`, `7117aa82` ORC-UF8: Hue- und DynaMount-Side-Car, FLIP, Soft-Key-Seiten
  V-POT/1-3, `4bfd526c` ORC-Reiter „Modes“ (offscreen gerendert, Layout geprüft).
- **Stufe 7** `40b3c1e2` TotalMix-Side-Car am UF8 in Rea-Sixty: Shift + LAYER 1, Bindungen v57,
  braucht ORCs Einstellungen wie das UF1-Side-Car. Nav-Kreuz Reihe/Submix/Fenster, Bank 8,
  Page 1, PLUG-IN Kanalansicht, Kanal-Encoder Main.
- **Handbuch** `d6b42dc8` ORC: Kapitel UF8, Seite Modes, Menüzeile UF8, Übergabe mit UF8 und
  Stativen, Dateien. `orc_manual.py --check` prüft jetzt jede UF8-Taste aus `Uf8Surface.cpp`.

Jeder neue Test gegen absichtliche Brüche geprüft. ctest 41/41. Extension auf dem Mac Studio
installiert (Hash `e731e66c`), ORC-Entwicklungsbuild in `extension/build/ORC.app`,
`/Applications` hat weiter ORC 1.2.3.

## Entscheidungen beim Bauen
- Keine neuen Builtin-Namen für die Side-Cars, `selection_mode_hue/_dynamount` bleiben.
- Presets nehmen die Gesten der Selection-Set-Bank (`selbank::op`), nicht nur kurz/lang.
- Der UF8-Start-Retry existierte schon seit v0.6.7 (`a3cee694`), nur mit `kSeenUf8`; die
  Memory sagte „nie wiederholt“ und war veraltet (korrigiert).

## Offen
- Release: Rea-Sixty zuerst (sonst bekommt ein altes Rea-Sixty den UF8 nie zurück), dann ORC
  mit UF8. Nur mit Franks Go.
- FLIP im TotalMix-Side-Car hat keine Belegung.
- Rea-Sixty-Handbuch erwähnt ORC und das TotalMix-Side-Car bewusst nicht (27.09.).
