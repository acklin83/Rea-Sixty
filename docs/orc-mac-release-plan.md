# ORC für den Mac ausliefern: Plan

Stand 28.09.2026. Frank: ORC wird ein **eigenes Produkt**, zuerst die **Mac-Version**.
Nichts davon ist gebaut.

## Ausgangslage (gemessen am Build vom 28.09.)

| Punkt | Stand | Folge beim Kunden |
|---|---|---|
| Plattform | CMake-Target `orc` nur in `if(APPLE)` | kein Windows-ORC |
| Architektur | nur arm64 | läuft auf keinem Intel-Mac |
| libusb | gelinkt gegen `/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib` | startet nicht ohne Homebrew-libusb |
| Signatur | ad hoc (`Signature=adhoc`, linker-signed) | Gatekeeper blockt die heruntergeladene App |
| Mindest-macOS | keine Angabe (`LSMinimumSystemVersion` fehlt) | unklar |
| CI | `.github/workflows/build.yml` nennt ORC nicht | kein Artefakt |
| Vertrieb | kein Kanal (ReaPack ist für REAPER-Erweiterungen) | kein Weg zum Kunden |

Auf dem Mac Studio vorhanden: Zertifikat
`Developer ID Application: Frank Acklin (234VF7874N)`.
Nicht vorhanden: ein `notarytool`-Zugang im Schlüsselbund (geprüft, keiner hinterlegt).

## Entschieden

- **Bundle-ID bleibt `ch.stoersender.orc`** (Frank 28.09.).
- Eigenes Produkt, Mac zuerst. Windows später (siehe unten).

## Schritte

1. **libusb ins Programm.** Eigene Kopie nach `ORC.app/Contents/Frameworks`, das
   Programm findet sie per `@rpath` (`@executable_path/../Frameworks`). Vorher klären,
   woher die Rea-Sixty-CI ihre libusb für macOS holt (Release-Assets
   `libusb-1.0.0.dylib` und `libusb-1.0.0-x86_64.dylib`), und dieselbe Quelle nehmen.
2. **Universal bauen** (arm64 + x86_64) für das ORC-Target, dazu ein Mindest-macOS.
   Untergrenze aus dem Code: macOS 11 (`imageWithSystemSymbolName` fürs
   Menüleisten-Symbol); die Menü-Überschriften ab macOS 14 sind schon abgesichert.
3. **Info.plist:** eigene ORC-Versionsnummer, `LSMinimumSystemVersion`, Copyright.
4. **Signieren** mit der Developer ID und Hardened Runtime, zuerst die mitgelieferte
   libusb, dann die App. Ohne Sandbox braucht ORC für USB und Netzwerk keine
   zusätzlichen Berechtigungen. Prüfen mit `codesign --verify --deep --strict` und
   `spctl --assess`.
5. **Notarisieren** mit `xcrun notarytool submit … --wait` über den Zugang im
   Schlüsselbund, danach `xcrun stapler staple`, damit die App auch offline startet.
6. **DMG** mit der App und einer Verknüpfung auf „Programme“; das DMG ebenfalls
   signieren, notarisieren und stapeln.
7. **Skript `tools/orc-release.sh`** für die Schritte 1 bis 6, wiederholbar und lokal.
   In der CI geht es nicht: Notarisieren braucht Franks Schlüsselbund.
8. **Prüfung wie beim Kunden:** DMG mit Quarantäne-Flag wie nach einem Download öffnen,
   auf einem anderen Benutzerkonto starten, UF1 und TotalMix verbinden.

## Einmal von Frank nötig

Den Notarisier-Zugang anlegen (App-spezifisches Passwort bei appleid.apple.com, dann
`xcrun notarytool store-credentials`). Den genauen Befehl gibt es mit dem Skript.

## Offen, Franks Entscheidung

- Erste Versionsnummer (Vorschlag 0.1.0).
- Mindest-macOS (Vorschlag 12).
- App-Symbol: im Finder und im DMG heute das leere Standardsymbol.
- Wohin der Download kommt: eigenes GitHub-Repo nur für Releases, oder eine Website.
- Markenrecherche zum Namen ORC (Klasse 9), laut Memory vor der Website.

## Nicht in diesem Plan (genannt, nicht gebaut)

- Start beim Anmelden.
- Automatische Updates.
- Eine Einführung, wie man TotalMix einrichtet (die Hilfetexte im Fenster sind auf
  Franks Wunsch entfernt).

## Windows, später

Aus Microsofts Übersicht „Code signing options for Windows app developers“ (Stand
29.08.2026):

- **Microsoft Store als MSIX:** gratis, weltweit, Microsoft signiert selbst, keine
  SmartScreen-Warnung. Offen und ungeprüft: ORC braucht unter Windows den
  WinUSB-Treiber für den UF1 (wie Rea-Sixty, per Skript mit Administratorrechten, danach
  verliert SSL 360 den UF1). Ob eine Store-App den Treiber mitbringen oder das Skript
  starten darf, ist die erste Frage.
- OV-Zertifikat: etwa 150 bis 300 $ pro Jahr mit Hardware-Token, weltweit, SmartScreen
  warnt bis ein Ruf aufgebaut ist.
- Azure Artifact Signing: Einzelpersonen nur USA und Kanada.
- SignPath Foundation: gratis, nur für Open-Source-Projekte.
- Das Fenster ist heute AppKit; unter Windows braucht ORC ein Tray-Symbol und ein
  eigenes Fenster. USB und TotalMix-Anbindung sind plattformneutral.
