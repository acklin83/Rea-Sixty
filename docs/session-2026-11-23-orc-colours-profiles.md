# Session 23.11.2026: ORC, Lampenfarben und Profile

Auslöser: Daves Post #18 im RME-Forum (ORC-Thread, 5 Punkte). Frank: 1 gibt es schon
(Namen kommen aus TotalMix), 2 und 3 bauen, 4 nicht machbar, stattdessen Export/Import
für Profilwechsel (Antwort auf 5). Plan vorgelegt, Entscheide: Profil-Inhalt A, Sicherung
vor dem Import ja, Endung `.orcprofile`.

## Gebaut

- **SEND/PLUG-IN-Farben:** `rme::KeyColours uf8SubmixLeds[8]` in `rme.json` (`uf8.submixLeds`,
  RGB als Hex, Helligkeit 0/1/2). Ein Maler-Entscheid: `Uf8Rme::submixKeyLed`, benutzt von ORC
  (`Uf8Surface::paintLeds_`) und Rea-Sixty (`main.cpp`, TotalMix-Side-Car). Cue bleibt gelb.
  Einstellungen: Spalten Active/Inactive auf der Seite UF8.
- **Bank-Tasten V-POT, 1-5:** `Config::rmeBankLed[10]` in `orc.json` (`rme_bank_leds`, nur
  abweichende, nur vom Besitzer der RME-Bänke geschrieben). Rea-Sixty liest über
  `applySideCarSourceLocked_`, Werkszustand über `restoreRmeSideCarBank`. Beide Maler
  (`bankKeyLamp_` in ORC, `sideCarBankKeyLamp_` in Rea-Sixty). Einstellungen: „UF8 key
  active/inactive“ im Kopf der Bank auf Soft Keys.
- **Profile** (`orc/OrcProfile.*`): Export… / Import… auf General. JSON mit base64 pro Datei.
  Inhalt: `orc.json`, `rme.json` ohne Verbindung, `moreme.json` nur Studio + Logo,
  Performer, Logo, `dynamount_presets`, `dynamount_fill`. Bleibt lokal: TotalMix-Verbindung,
  MoreMe-Schalter/Ports/PIN, Hue/OBS samt Schlüsseln, DynaMount-Geräte und -Positionen.
  Import: prüfen, Rückfrage im Fenster, Kopie nach `profile-import.orcprofile`, ORC startet neu
  (`requestRestart`, `relaunch` Mac/Windows), `applyStaged()` ganz am Anfang von `main()`,
  vorher Sicherung nach `profiles/before-import-<Zeit>.orcprofile`. Danach öffnet ORC das
  Fenster mit der Meldung.
- `PhonesServer`: `parseSettings`/`settingsJson` freigelegt, damit der Import kein zweites
  Format schreibt.
- Handbuch (`orc/manual/template.html`): SEND/PLUG-IN, V-POT 1-5, Seite UF8, Soft Keys,
  General/Profiles, Files. Bilder General und Soft Keys neu (offscreen WebKit, Demo-Daten,
  Fenster 1280 auf 1600 skaliert, Ports nur in der Anzeige auf 7005/7006).

## Entscheidungen

- Import erst beim nächsten Start: `closeDown` schreibt `modes` und `rme.json` noch einmal,
  ein Schreiben unter dem laufenden ORC würde überschrieben.
- Was ein Profil nicht hat (Performer, DynaMount-Presets), wird beim Import geleert.
- Sicherungsname englisch (`before-import-…`), ORC ist englisch; im Plan stand `vor-import`.

## Gefunden beim Rendern

- Der Kopf der Soft-Keys-Bank lief in Standardbreite über die Karte. `.skhead` schrumpft
  jetzt Shows und Name (`minmax(120px,220px)`).
- `orc_settings_demo` kann in einem frischen Scratch-HOME `rme.json` nicht schreiben
  (`Library/Application Support` fehlt, `supportDir` legt nur den letzten Ordner an). Alt,
  nur die Demo, nicht angefasst.

## Offen

- ORC in /Applications nicht getauscht (nur auf Franks Wort).
- Windows: nur von CI gebaut, Dialoge und Neustart ungetestet am StoerPC.
- Website `/orc` nimmt zwei ORC-Bilder; ob General/Soft Keys darunter sind, nicht geprüft
  (Branch `website`).
- Antwort an Dave im Forum.

## Nachtrag: Tausch und Release 1.8.0

- Dev-Build `f0237a86` mit Developer ID nach /Applications (ORC beendet, Anmeldeobjekt blieb).
- ORC 1.8.0 (Build 16, `989e3f0a`) released: https://github.com/acklin83/ORC/releases/tag/v1.8.0.
  App + DMG notarisiert, im DMG geprüft (1.8.0, Build 16, Profile drin), Windows-Installer aus
  CI, Handbuch ORC `53d01ea` (Pages live mit 1.8.0), Website-Knopf `fcd562ed` nach `private`
  gepusht und gebaut (verify-build grün).
- Website-Deploy auf den VPS sperrte der Auto-Modus: Frank deployt.
- Forenantwort an Dave als Entwurf an Frank, nicht gepostet.
- Die ORC-Seite der Website bindet keine `docs/screenshots` mehr ein (offener Punkt oben erledigt).

## 25.11.2026: Namen aus der Datei des gezeigten Geräts

- Forum #20 (Dave): zwei `last.FirefaceUFXIII<Serie>.xml` nach Garantietausch, TotalMix schreibt
  laut ihm beide, ORC nahm die jüngste = alte, unbenannte Datei ("Layout 1-6", "Mix 1-8").
- Fund an Franks Mac: `rme.totalmix.default.xml`, `Default0/Cache0`, `DeviceName` =
  "Fireface UFX+ (23802132)", ohne Leerzeichen und Klammern = Dateiname.
- `RmeNames`: `stateFileKey`, `shownDevice`, `pickStateFile` (rein, Tests in test_rme_osc);
  `namesFromDisk` nimmt die Datei des gezeigten Geräts, sonst wie bisher die jüngste.
- Ungeprüft: Daves default.xml, mehrere Fenster (`Default1`…), Windows-Aufbau. Frage an Dave in
  der Forenantwort.

## 25.11.2026: Pan der Ausgänge

- Frank: Pan der Control-Room-Ausgänge oft verschoben (Phones 2 R3, Phones 3 R1, Main R1), und
  von Hand kein C, nur L1/R1. Ursache C: ein Schritt = 2 % mal V-Pot-Speed, Franks UF8 auf 0.75
  = 1,5 %, ungerundet; −0,005 zeigt schon L1. Ursache Verschieben: kein Schreiber ohne Hand im
  Code, Werte passen zu einzelnen 1,5-%-Schritten (gestreifter V-Pot vermutet, nicht belegt).
- `rmeu::panTurn`: ganze Prozent, Mitte rastet beim Überqueren; Breite ohne Mitte. Benutzt vom
  UF1-Pot über dem Fader, den UF8-V-Pots und der Kanalseite (`RmeStrip::nudge`).
- `rmeu::logOutputPan` → `/tmp/rme_pan.log`: jede Änderung an `/output/<n>/balpan` mit Quelle,
  auch Pushes. Nur Diagnose.
- Nicht gebaut: Schutz gegen gestreifte V-Pots (genannt, Frank entscheidet).

## 25.11.2026: ORC 1.8.1

- ORC 1.8.1 (Build 17, `f7209c8b`) released: https://github.com/acklin83/ORC/releases/tag/v1.8.1,
  mit Gerätedatei-Fix (`9700b59a`) und Pan (`eb6d00dd`). App + DMG notarisiert, im DMG geprüft,
  Windows-Installer aus CI, Handbuch ORC `d3b021f`, Website-Knopf `ff2128a6` (private, gebaut,
  Deploy durch Frank). Aus dem DMG nach /Applications (ORC von Frank beendet, Anmeldeobjekt blieb).
