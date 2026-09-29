# Session 29.09.2026: Reverb und Echo als FX-Reihe

Plan: `docs/orc-reverb-echo-plan.md`. Gilt für den Side-Car in Rea-Sixty und für ORC.

## Gebaut

- **Vierte Reihe FX** unter den Outputs (`Row::Fx = 3`, `kRowCount = 4`), zwei Kanäle
  Reverb (0) und Echo (1) aus `State::fx`. Nav auf/ab und MODE + Encoder laufen durch vier
  Reihen mit Umlauf.
- Auf der FX-Reihe: Fader = Volume in dB (`/reverb/volume`, kein faderlin), Pot über dem
  Fader = Width 0..1 (Druck = 1.0), CUT = Effekt aus (`enable` invertiert), SOLO / SEL /
  Stereo-Taste nichts, kein Pegel, Wertzeile „Width +0.60", Farbbalken „FX".
- **STRIP-Seiten** „Reverb", „Reverb 2", „Reverb 3" (Zeilen `reverb`) und „Echo" (`echo`).
  Regler je Reverb-Typ nach TotalMix' FX-Fenster (`Param::revTypes`). Nach einem
  geschriebenen Reverb-Typ `/sendall`, weil der Typ Werte lädt.
- **Output 2** mit FX Return, direkt nach „Output", gepackt: FX Ret nach Pan, Xfeed,
  Delay, Ref Lvl.
- **`rme_mute_fx`** im Picker (Kategorie RME über das Präfix), Lampe aus `/controlroom/mutefx`.
- **`rme.json` Version 5**: Output 2 und die FX-Seiten kommen in bestehende Dateien
  (`upgradeStripPagesToV5`), nur einmal und nur wenn sie fehlen.

## Nach Franks erstem Blick

- Echo hatte nur Sekunden: neuer Regler **BPM** (`Kind::Bpm`, 60 / Delay, schreibt Sekunden),
  Seite „Echo 2" mit Width und HiCut.
- Reverb hatte kein Width im STRIP: `rev_width` nach Smooth, `echo_width` nach Feedback.
  Der Pan-Pot bleibt zusätzlich Width.

## Gefunden beim Bauen

- `input::State::sel` hatte **drei** Plätze. `sel[Row::Fx]` hätte über das Ende gelesen.
  Jetzt `sel[kRowCount]` mit `static_assert`.
- `OrcSettingsWindow.mm`: `kinds[]` und `headers[]` mit drei Einträgen in einer Schleife über
  `kRowCount`. Jetzt `kMixerRowCount = 3` für alles, was TotalMix-Kanäle meint.
- Neun `std::clamp(row, 0, 2)` ersetzt durch `rmeu::rowAt`.
- Die Werksseite „Output" steht nicht mehr allein; der Test „a lone page keeps its slots"
  prüft den Fall jetzt mit einer Liste ohne „Output 2".

## Tests

`test_rme_osc` (Zustand, jede FX-Verzweigung, Seiten pro Typ, Texte, v5-Hochstufung),
`test_rme_input` (Reihenumlauf über vier, CUT, Width, STRIP auf FX), `test_rme_face`
(Fader schreibt dB, CUT-Lampe, Wertzeile, kein Pegel). Gegenprobe: FX-Fader auf faderlin
zurückgedreht, zwei Tests fielen um. ctest 19/19.

## Offen

- Das Handbuch hat kein Kapitel zum RME-Side-Car; die FX-Reihe steht darum nirgends für Nutzer.
- 10 der 15 Reverb-Typen sind als „wie Large Room / Shorty" angenommen, nicht gesehen.
- Ob der Echo-Typ Werte lädt wie der Reverb-Typ, war im Test nicht zu sehen.
