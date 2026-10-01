# Session 01.10.2026: Fixed Lanes, ORC 1.2, TotalReaper 0.2.6

## Gebaut

**Fixed Lanes** (Plan `docs/fixed-lanes-plan.md`, Reihenfolge Abschnitt 5)
- `f24ebb9` Schritt 2: Kern `LaneModel.h` + `test_lane_model` (Spielsatz, Steppen, A/B mit
  wartender Schichtung, Razor pro Lane, Comp-Karte aus `LANEREC` / `LINKEDLANE`).
- `f05ddc6` Schritt 3 (Baustein H): `EncoderRing.h` für UF1 und UF8; Zoom-Pad + ENC PUSH pro
  UF8-Encoder-Modus (90 ButtonIds, Bindings v51, Rückfall auf die physische Taste); ENC PUSH
  halten + drehen = Karussell, Anzeige auf den Scribbles; ENC PUSH feuert beim Loslassen.
  Dropdown „Encoder mode" und Ring-Editor in den Settings. Tests `encoder_ring`, `uf8_enc_keys`.
- `9ec17c6` Schritt 4 (B + E + G): Jog-Modus Lanes (UF1), Encoder-Modus Lanes (UF8, UF1),
  13 `lane_*`-Builtins, Kreuze, Bindings v52, Handbuch-Kapitel „Fixed lanes" mit den
  Fallstudien, die heute gehen. UF1-Jog-Ring auf `EncoderRing` (gespeicherte Reihenfolge
  überlebt neue Modi).

**ORC**
- `19ec4f8` ORC 1.2.0 veröffentlicht: Output-Seite ohne Pan, FX Ret darauf (8 statt 9 Seiten,
  `rme.json` v6); 360 öffnet und schliesst die Einstellungen; Menüzeile mit Version.
- `4e56928` ORC 1.2.1 (lokal installiert, NICHT veröffentlicht): Zeitstempel in `/tmp/orc.log`,
  Rea-Sixty schreibt seine Seite der Übergabe in dieselbe Datei.

**TotalReaper 0.2.6** (`8914960` im TotalReaper-Repo, Release + ReaPack): Absturz in `Run()`
behoben (fällige Aktionen planten während der erase-Schleife neue ein, der Vektor zog um).

## Offen

- Lanes, von Frank am Gerät zu prüfen (ungemessen): Spielzustand per API schreiben, Comp here
  bei ausgeschaltetem Comping, 42708 = runter, Gruppen bei 42707/42708. Danach Schritt 5
  (Malen), dann C (Bank „Lanes"), D (Strips), Takes in Items.
- Lanes: UF8-Wertzeile zeigt beim Steppen keinen Lane-Namen; Einstellungs-UI fehlt.
- Aufnahme-Aussetzer (REAPER + OBS) am Abend, nicht gefunden. ORC-Log zeigt nachmittags
  `LIBUSB_ERROR_NO_DEVICE` beim UF1 und 13 Neuöffnungen ohne Pause. Mit Zeitstempeln jetzt
  beim nächsten Mal einzuordnen. Nächstes Mal: nichts beenden, live messen.
- ORC 1.2.1 veröffentlichen, wenn Frank will.
- CI-Linux brauchte einmal 28 Minuten für `apt-get` (Paketserver), kein Code-Problem.
