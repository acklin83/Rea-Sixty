# ORC für den Mac ausliefern: Plan

Stand 28.09.2026, ergänzt 29.09. um die zwei Nutzergruppen. Frank: ORC wird ein **eigenes
Produkt**, zuerst die **Mac-Version**. Nichts davon ist gebaut.

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
`Developer ID Application: Frank Acklin (234VF7874N)` und der Notarisier-Zugang
`rea-sixty-notarize`, mit dem jede Rea-Sixty-Version notarisiert wird
(`dist/release-mac.sh`). Geprüft 28.09.: `notarytool history` antwortet, letzte
Einreichung v0.6.1 am 27.09., Accepted.

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
5. **Notarisieren** mit `xcrun notarytool submit … --keychain-profile rea-sixty-notarize
   --wait`, danach `xcrun stapler staple`, damit die App auch offline startet.
   Vorbild für Aufbau und Prüfungen: `dist/release-mac.sh`.
6. **DMG** mit der App und einer Verknüpfung auf „Programme“; das DMG ebenfalls
   signieren, notarisieren und stapeln.
7. **Skript `tools/orc-release.sh`** für die Schritte 1 bis 6, wiederholbar und lokal.
   In der CI geht es nicht: Notarisieren braucht Franks Schlüsselbund.
8. **Prüfung wie beim Kunden:** DMG mit Quarantäne-Flag wie nach einem Download öffnen,
   auf einem anderen Benutzerkonto starten, UF1 und TotalMix verbinden.

## Zwei Nutzergruppen (29.09.2026)

Frank: „Zwei User-Gruppen: eine hat Rea-Sixty, eine hat Rea-Sixty nicht. Beide wollen den
UF1 auch standalone für ORC nutzen." Beide bekommen **dieselbe ORC.app aus demselben
Download**. Was sich unterscheidet, ist das Drumherum.

### Gruppe A: nur ORC

| Braucht | Stand |
|---|---|
| ORC.app, die auf einem fremden Mac startet | fehlt (Schritte 1 bis 8 oben) |
| TotalMix eingerichtet: Global OSC, eine Remote auf 7005 / 7006, „In Use" | ORC sagt es nicht mehr (Hilfetexte auf Franks Wunsch entfernt), also braucht es eine kurze Einrichtungsseite beim Download |
| SSL 360 geschlossen, UF1 angesteckt | dieselbe Bedingung wie bei Rea-Sixty; gehört auf dieselbe Seite |
| Wo melde ich Fehler | offen |
| Wie erfahre ich von einer neuen Version | kein ReaPack, also offen |

### Gruppe B: ORC und Rea-Sixty auf demselben Mac

| Punkt | Stand |
|---|---|
| Wer hält den UF1 | **gelöst, seit v0.6.1 bei den Nutzern** (`4a68823`, „Weg c"): Rea-Sixty legt beim REAPER-Start `~/Library/Application Support/ORC/handover` mit seiner pid an, ORC gibt UF1 und TotalMix-Port frei und holt beides zurück, wenn REAPER beendet wird. Einstellung in Rea-Sixty „Take the UF1 over from ORC", ab Werk an. |
| TotalMix-Remote | beide Programme benutzen dieselbe Remote (7005 / 7006); der Port geht mit dem UF1 über |
| Einstellungen | **getrennt**: ORC hat `ORC/rme.json` + `orc.json`, Rea-Sixty `REAPER/rea_sixty/rme.json` + `bindings.json`. Franks eigene zwei Dateien sind schon auseinandergelaufen (Main B und Main in Bank 2 vertauscht). |
| ORC muss laufen, damit es den UF1 nach REAPER übernimmt | kein Start beim Anmelden (steht oben unter „nicht in diesem Plan") |
| Gleicher Funktionsstand | Side-Car und ORC teilen den Code. Seit v0.6.1 sind 21 Commits dazugekommen, darunter die FX-Reihe. ORC 0.1.0 und Rea-Sixty 0.6.2 sollten aus demselben Commit kommen. |
| Rea-Sixty unter Windows / Linux | kein ORC (nur Mac) |

### Für beide

- **Lizenzhinweise:** ORC liefert libusb (LGPL-2.1) mit wie Rea-Sixty. Der Plan
  `docs/third-party-notices-plan.md` gilt für das DMG vom ersten Tag an: Hinweise, LGPL-Text,
  libusb-Quelltext am Download-Ort.
- **Gratis oder kostenpflichtig** entscheidet über den ganzen Weg. Kostenpflichtig heisst:
  (1) `docs/interop-rationale.md` verlangt vor einer kommerziellen Verbreitung eine neue
  rechtliche Prüfung, und SSLs Zustimmung vom 18.05. galt einem offenen, nicht kommerziellen
  Projekt; (2) der ORC-Quelltext liegt öffentlich unter MIT im Rea-Sixty-Repo, jeder kann ihn
  bauen; (3) eine Lizenzprüfung in der App. Gratis heisst: nichts davon.
- Kundentexte (Einrichtungsseite, Ankündigung) mit dem Skill `texte`.

### Reihenfolge

1. Entscheidungen unten.
2. Schritte 1 bis 8 oben, dazu die Lizenzhinweise im DMG.
3. Frank prüft wie ein Kunde: Gruppe A auf einem zweiten Benutzerkonto ohne REAPER,
   Gruppe B mit REAPER und Rea-Sixty (Übergabe hin und zurück).
4. ORC 0.1.0 und Rea-Sixty 0.6.2 aus demselben Commit.
5. Einrichtungsseite und Ankündigung.

## Offen, Franks Entscheidung

- **Gratis oder kostenpflichtig** (siehe oben, entscheidet alles andere mit).
- **Gruppe B, Einstellungen:** getrennt lassen, oder ORC übernimmt beim ersten Start die
  Side-Car-Einstellungen aus Rea-Sixty einmalig.
- **Start beim Anmelden** als Schalter in ORC (für Gruppe B nötig, damit ORC nach REAPER
  übernimmt). Macht ihn das Mindest-macOS 13, weil Apples `SMAppService` erst ab 13 da ist
  (nicht geprüft, vor dem Bau nachlesen)?
- **Update-Hinweis** für Gruppe A: keiner in 0.1.0, oder ORC fragt beim Start eine kleine
  Datei am Download-Ort ab.
- **Wo Fehler gemeldet werden.**
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
