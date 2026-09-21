# Session 21.09.2026: UF1-Ebenen, RME-Side-Car, v0.6-Send-Strips

## Gemacht

**UF1-Ebenen vermessen** (Runbook `docs/uf1-layout-probe-runbook.md`): Layout 1 zeigt
vier Farbbalken, V-Pot-Reihe nur mit Stil 0x02/0x03/0x04, Kappung nach Pixelbreite;
Layout 2 hat `0x011b` als 2-/4-stelliges Zahlenfeld, keine Farbbalken. DAW-Ansicht auf
Layout 1 geplant und geparkt (`docs/uf1-daw-view-layout1-plan.md`).

**TotalMix gemessen**: Farbindizes, EQ-Typen, Low-Cut-Steilheit, Faderkurve (dann RMEs
eigene Formel), Remote 3 (7005/7006), kein Echo an den sendenden Remote,
Parametergrenzen aus RMEs UFX+-Handbuch (`docs/rme-strip-and-uf8-plan.md` 5b).
`rme_osc_probe` kann jetzt filtern, live mitschreiben und gezielt senden.

**RME-Side-Car gebaut** (Plan `docs/uf1-spread-plan.md`): `RmeManager` im Tick,
`rme.json`, Settings → Modes → RME; Maler mit V-Pots (Phones 1-4), Fader auf dem
gewählten Kanal, drei Reihen, Submix per V-Pot-Druck, Jog = Main, Kanal-EQ als Graph,
Kopfzeile, TotalMix-Pegel; eigene Soft-Key-Bänke pro Side-Car-Modus (RME, Item Volume),
Builtins `rme_dim`, `rme_mono`, `rme_speaker_b`, `rme_talkback`, `rme_fader_main`.

**CI-Prüfung erweitert**: `check_builtin_docs.py` verlangt jetzt auch eine
Picker-Kategorie; fand dabei zwei ältere unsichtbare Builtins.

**v0.6**: UF8- und UF1-Extender-Send-Strips zeigen Pegel, GR, Nummer und Farbe der
Spur des Sends; SEL tut dort nichts. Auf `release/0.6` (ab `87edabf`) per cherry-pick.

## Entscheidungen (Frank)

- Side-Car auf Layout 3 (EQ-Graph), keine Farbbalken im Monitor-Teil.
- Alles einstellbar; eigene Bänke pro Side-Car-Modus; V-Pot-Druck wählt den Submix.
- Kanalansicht: Graph nur auf EQ/Low-Cut-Seiten über Layout 1 (Weg B), Soft-Keys =
  Schalter der Seite; UF8-V-Pots in der Input-Reihe = Gain.
- TotalMix-Wege zusammenfassen, aber nicht am UF1-Side-Car aufhängen.
- Tag v0.6.0 geht auf `release/0.6`.

## Offen

- Kanalansicht (STRIP) auf dem UF1, dann `RmeSurface`, UF8 `SelectionMode::TotalMix`.
- Plan zur Zusammenführung TotalReaper / stoerme / Rea-Sixty-RME / ORC.
- Dynamische Bankarten auf Side-Car-Bänken werden ausgelöst, aber nicht beschriftet.
- Farbzuordnung TotalMix → UF1 ist ein Platzhalter, Frank ordnet selbst zu.
- `RmeManager`: Herzschlag über `/status/*` statt `/sendall` alle 30 s.
