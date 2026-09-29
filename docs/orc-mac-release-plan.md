# ORC für den Mac ausliefern: Plan

Stand 28.09.2026, ergänzt 29.09. um die zwei Nutzergruppen und Franks Entscheidungen. Frank: ORC wird ein **eigenes
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
2. **Universal bauen** (arm64 + x86_64) für das ORC-Target, Mindest-macOS **13**
   (`CMAKE_OSX_DEPLOYMENT_TARGET`), wegen `SMAppService`. Die Menü-Überschriften ab
   macOS 14 sind schon abgesichert.
3. **Info.plist:** Version 1.0.0, `LSMinimumSystemVersion` 13.0, Copyright.
3a. **Start beim Anmelden:** ein Schalter im ORC-Fenster, `SMAppService.mainApp`
   `register` / `unregister`, der angezeigte Zustand kommt aus `status` (der Nutzer kann
   ORC auch in den Systemeinstellungen abschalten, das muss der Schalter zeigen). Ab Werk aus.
3b. **Lizenzhinweise** im DMG und im Repo, nach `docs/third-party-notices-plan.md`
   (libusb LGPL-2.1: Hinweis, Lizenztext, Quelltext beim Release).
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
9. **Repo `acklin83/ORC`** anlegen: README als Einrichtungsseite (Text mit dem Skill
   `texte`), Lizenz, Lizenzhinweise, Issues an, Release 1.0.0 mit DMG und libusb-Quelltext.

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
| Einstellungen | **eine Quelle, ORC** (seit 25.09.2026, Frank: „Die ganze Konfig für den RME Side-Car kommt in ORC"). ORC schreibt `ORC/rme.json` und `orc.json`, Rea-Sixty liest beide nur (`reasixty_rmeConfigPath_`, `reasixty_orcBindingsPath_` in main.cpp). `REAPER/rea_sixty/rme.json` ist ein Überbleibsel von vorher, das nichts mehr liest. |
| Side-Car in Rea-Sixty ohne ORC | **gibt es nicht**: ohne `ORC/rme.json` bietet Rea-Sixty keinen Side-Car an. Wer ihn in REAPER will, installiert ORC. Unter Windows und Linux gibt es ihn darum heute gar nicht. |
| ORC muss laufen, damit es den UF1 nach REAPER übernimmt | kein Start beim Anmelden (steht oben unter „nicht in diesem Plan") |
| Gleicher Funktionsstand | Side-Car und ORC teilen den Code. Seit v0.6.1 sind 21 Commits dazugekommen, darunter die FX-Reihe. ORC 0.1.0 und Rea-Sixty 0.6.2 sollten aus demselben Commit kommen. |
| Rea-Sixty unter Windows / Linux | kein ORC (nur Mac) |

### Für beide

- **Lizenzhinweise:** ORC liefert libusb (LGPL-2.1) mit wie Rea-Sixty. Der Plan
  `docs/third-party-notices-plan.md` gilt für das DMG vom ersten Tag an: Hinweise, LGPL-Text,
  libusb-Quelltext am Download-Ort.
- **Gratis** (Frank 29.09.). Kostenpflichtig hätte eine neue rechtliche Prüfung verlangt
  (`docs/interop-rationale.md`, SSLs Zustimmung galt einem offenen, nicht kommerziellen Projekt).
- Kundentexte (Einrichtungsseite, Ankündigung) mit dem Skill `texte`.

### Reihenfolge

1. Entscheidungen unten.
2. Schritte 1 bis 8 oben, dazu die Lizenzhinweise im DMG.
3. Frank prüft wie ein Kunde: Gruppe A auf einem zweiten Benutzerkonto ohne REAPER,
   Gruppe B mit REAPER und Rea-Sixty (Übergabe hin und zurück).
4. ORC 0.1.0 und Rea-Sixty 0.6.2 aus demselben Commit.
5. Einrichtungsseite und Ankündigung.

## Entschieden am 29.09.2026 (Frank)

- **Gratis.** Keine Lizenzprüfung in der App, keine neue Rechtsprüfung nötig.
- **Name bleibt ORC.** Markenrecherche gemacht (WIPO Global Brand Database, 29.09.): „ORC" ist
  in Klasse 9 mehrfach eingetragen (u.a. Itiviti / ORC Software, SE, Handelssoftware, US
  3927831; ORC Manufacturing, JP, US 4656179). Geprüft und verworfen: UFRC (frei, aber
  SSL-Präfix), ORCHID, MORC, TORC, OSTRA (alle in Kl. 9 belegt). Frank: „wir nehmen ORC".
  Damit bleiben Ordner `~/Library/Application Support/ORC/`, Bundle-ID und Übergabe-Marke,
  wie sie sind; nichts zieht um.
- **Version 1.0.0.**
- **Start beim Anmelden: ja**, als Schalter in ORC über `SMAppService.mainApp` (Apple:
  macOS 13.0+, nachgelesen im SDK-Header `SMAppService.h` und in Apples Doku). Damit ist das
  **Mindest-macOS 13**.
- **Update-Hinweis: später**, nicht in 1.0.
- **Download und Fehlermeldungen** (Frank: „das ist dein Gebiet"): ein eigenes öffentliches
  GitHub-Repo `acklin83/ORC`, nur für Auslieferung. Darin: README als Einrichtungsseite
  (TotalMix Global OSC, Remote auf 7005 / 7006, SSL 360 zu), Lizenz und Lizenzhinweise,
  die Releases mit dem DMG und dem libusb-Quelltext, Issues für Fehlermeldungen. Kein Code:
  der bleibt im Rea-Sixty-Repo, weil ORC den Side-Car-Code mit der Extension teilt. Grund:
  kostet nichts, feste Adresse `github.com/acklin83/ORC/releases/latest`, Fehlermeldungen
  mit Anhängen, und es trägt keinen Rea-Sixty-Namen.

## Offen

- **App-Symbol** (Frank: „machen wir noch"). Bis dahin zeigen Finder und DMG das Standardsymbol.

## Nicht in diesem Plan (genannt, nicht gebaut)

- Automatische Updates und ein Update-Hinweis (später, Frank 29.09.).
- Hilfetexte im ORC-Fenster (auf Franks Wunsch entfernt); die Einrichtung steht in der
  README des Repos.

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
