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
