# Session 03.11.2026: ORC für Windows

Plan: `docs/orc-windows-plan.md`, von Frank freigegeben. Alle Stufen gebaut, auf Branch `claude/orc-windows-59c1a5`, nichts auf `main`, keine Versionsnummer geändert. Frank testet als `sunny` am StoerPC.

## Was steht
- `ORC.exe`, x64, Installer `ORC-<version>-Setup.exe` (NSIS), Workflow `.github/workflows/orc-windows.yml`.
- Neu: `src/OrcFiles.h`, `src/WinUsbDriver.{h,cpp}`, `orc/OrcSock.h`, `orc/OrcMenu.{h,cpp}`, `orc/OrcTray.cpp`, `orc/OrcWebWindow.cpp`, `orc/OrcWinUi.h`, `orc/win/*`, `tests/TestScratch.h`, `tools/orc-release-win.sh`.
- Rea-Sixty: Übergabe und RME-Side-Car unter Windows frei, Treiber-Code aus `main.cpp` ausgezogen, Gerätenamen neutral.
- Handbuch: Kapitel „ORC under Windows“.

## Geprüft
Mac: `ctest` 47 von 47, `orc_manual.py --check`. CI Windows: Bau, 10 Tests, Installer. Am StoerPC per SSH unter `claude`: `ORC.exe` startet ohne Konsole, Log, Rechte auf `%APPDATA%\ORC`, Abhängigkeiten.

Nicht gelaufen, geht nur mit Frank am Gerät: Taskleiste, Fenster, UF1 und UF8, TotalMix, Treiber-Prompt, Firewall, Übergabe, Installer. Abschnitt 17 im Plan.

## Entscheide beim Bau
- Eigener Workflow statt `build.yml`, Schalter `ORC_WINDOWS`.
- QR als Owner-Draw statt `MIIM_BITMAP`.
- Treiber fragt auf der Karte nach. Die Seite hat keine Browser-Dialoge, `test_orc_settings` prüft das.
- `RmeNames` liest die TotalMix-Namen auch unter Windows, `%LOCALAPPDATA%\TotalMixFX`. Stand nicht im Plan.
- `vcruntime140.dll` neben `ORC.exe`, weil die fertige libusb-DLL sie braucht.

## Offen für das Release
- Frank: SignPath beantragen.
- Versionsnummer, Build-Nummer, Rea-Sixty-Release mit Side-Car unter Windows, Changelog.
- Website `orc.astro` („Mac only“), `~/Documents/dev/ORC/README.md`, Handbuch nach `ORC/docs`.
- Merge nach `main` nach Franks Test.

## CI, Nachtrag
`test_orc_phones_server` hing am Zufall: eine offene Runde verwirft, was für einen eben geschriebenen Knoten zurückkommt, und der Test bestand nur, weil die Nachfrage 100 ms nach dem Schreiben fiel. Mit 300 ms Verschiebung 3 von 3 rot, seit `7c8f4aff` wartet der Test auf die nächste Abfrage. Dieselbe Logik liegt auf `main`, dort ist der Test noch nicht korrigiert, der Merge bringt ihn mit.

## Nachtrag 04.11.: Franks Test am StoerPC, Release

Getestet von Frank: Installer, UF8, Übergabe an REAPER und zurück, MoreMe mit offline laufendem TotalMix. Die UFX+ war nicht am PC; Frank: kein Known Issue, offline zeigt dasselbe.

Zwei Fehler aus dem Test, beide behoben:
- **MoreMe ohne Kanäle.** Mit allen Kanälen im Layout dauert eine Runde 2 s, die Submix-Punkte kommen ab 630 ms (am StoerPC gemessen, 60 Eingänge, 47 Playbacks, 47 Ausgänge, 1 MB). Das feste 400-ms-Fenster sah keinen. Eine Runde endet jetzt 400 ms nach ihrer letzten Nachricht, die nächste beginnt danach (`d0b405e7`). Eine zweite Abfrage startet die Runde bei TotalMix neu, zwei Remotes laufen unabhängig (gemessen). Warum der alte Build alle 10 s kurz Kanäle zeigte, ist nicht geklärt.
- **Flächen 15 s bis oben.** `Sleep(2)` dauert 15.5 ms, mit `timeBeginPeriod(1)` 2.4 ms (gemessen). ORC fordert den Timer an, und weil Windows 11 das bei Prozessen ohne sichtbares Fenster ignorieren darf, auch `SetProcessInformation` (`b996f1b2`). In REAPER war es laut Frank nicht langsam, die DLL bleibt.

SignPath hat abgelehnt, ORC und Rea-Sixty bleiben unter Windows unsigniert. Offen für v0.9.2: Übergabe erst nach geladenem Projekt.
