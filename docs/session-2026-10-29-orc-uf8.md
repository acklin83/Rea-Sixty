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

Jeder neue Test gegen zwei absichtliche Brüche geprüft. ctest 36/36. Extension auf dem Mac
Studio installiert (Hash geprüft).

## Entscheidungen beim Bauen
- Keine neuen Builtin-Namen für die Side-Cars, `selection_mode_hue/_dynamount` bleiben.
- Presets nehmen die Gesten der Selection-Set-Bank (`selbank::op`), nicht nur kurz/lang.
- Der UF8-Start-Retry existierte schon seit v0.6.7 (`a3cee694`), nur mit `kSeenUf8`; die
  Memory sagte „nie wiederholt“ und war veraltet (korrigiert).

## Offen
- 3b: Hue- und DynaMount-Maler und -Eingang aus `main.cpp` herausziehen (ohne Verhaltens-
  änderung), Voraussetzung für ORC.
- Stufe 5 ff.: UF8 in ORC, TotalMix-Side-Car, Kanalansicht, UF1 + UF8 als ein Pult.
- Release: Rea-Sixty mit Stufe 1 muss vor ORC mit UF8 raus.
