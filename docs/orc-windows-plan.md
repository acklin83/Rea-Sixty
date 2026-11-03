# ORC für Windows, Plan

Stand 03.11.2026. Plan freigegeben (Frank 03.11.: „Plan ok, alle Empfehlungen so, teste als sunny“), im Bau. Entscheid Frank: Mac-Release ORC 1.6.0 / Rea-Sixty v0.9.0 zuerst und ohne Windows. Windows danach als eigene ORC-Version, sobald er es am StoerPC getestet hat. Linux ist gestrichen.

## Ziel
`ORC.exe`, x64, Windows 10 1903 oder neuer und Windows 11, kann alles, was ORC am Mac kann: UF1 und UF8, TotalMix, MoreMe, Studio Control, Hue, OBS, DynaMount, Übergabe an Rea-Sixty. Dieselbe Seite, dasselbe Modell, dieselben Dateiformate. Neu sind nur Dinge, die Windows erzwingt: Treiber-Karte auf der Seite und ein Installer.

## 1. Bestand, selbst gezählt

| Stelle | Was ist Mac-gebunden | Weg unter Windows |
|---|---|---|
| `orc/OrcApp.mm` (280 Zeilen) | Statusitem, Menü, QR, Quit | neu `orc/OrcTray.cpp`, Texte teilen (Abschnitt 2) |
| `orc/OrcWebWindow.mm` (214) | Fenster, Open at login, Logo-Dialog, Edit-Menü | neu `orc/OrcWebWindow.cpp` |
| `orc/PhonesServer.cpp` | 11 Mac-Includes, rund 25 Stellen im Code: Sockets, `arc4random`, `fchmod`, `_NSGetExecutablePath`, `CFBundle`, `getifaddrs`, `errno` | `orc/OrcSock.h`, `OrcConfig` |
| `orc/SettingsServer.cpp` | 6 Includes, rund 14 Stellen | dito |
| `orc/main.cpp` | `isatty`, `dup2`, `setvbuf`, Signale, Abschluss nach `runApp` | Windows-Zweige, Abschluss als eigene Funktion |
| `orc/OrcConfig.cpp`, `OrcModes.cpp` | `HOME`, `mkdir`, `getpid`, `kill`, `rename` | Abschnitt 4 und 8 |
| `orc/OrcLog.h`, `OrcKeyboard.h`, `OrcLogo.*` | `gettimeofday`, CoreGraphics | Abschnitt 2 und 12 |
| `extension/CMakeLists.txt` | Ziel `orc` unter `if(APPLE)`, Testblock unter `if(NOT WIN32)` | Abschnitt 14 |
| Rea-Sixty `src/main.cpp` | 9 mal `#ifdef __APPLE__` in der Übergabe (1809 bis 2031), 2 Pfade (5020, 5033). `SettingsScreen_General.cpp:667` | Abschnitt 8 |

Baut MSVC schon heute für die DLL: alle Dateien aus `ORC_SHARED_SOURCES` ausser `src/macos_http.mm`, dafür gibt es `src/win_http.cpp`. Hat MSVC nie gesehen: `Surface`, `Uf8Surface`, `Phones`, `Performers`, `WebSocket`, `OrcSettingsModel`, `SettingsServer`, `PhonesServer`, `OrcConfig`, `OrcModes`.

Am StoerPC gemessen (nur lesend, SSH): Windows 11 Pro 10.0.26200, WebView2-Laufzeit 156, Smart App Control aus. UF1-009184 und UF-001254 stehen auf WinUSB, `oem2.inf` ist unser Paket, `sslbus.inf` liegt daneben. UFX+ war schon mal dran. Jetzt ist weder UF1, UF8 noch RME angesteckt. `TotalMixFX_x64.exe` läuft aus `C:\Program Files\RME\MADIface`, Datei meldet 0.2.0.3, ohne offenen UDP-Port. Angemeldet ist `frank`, die aktuelle Rea-Sixty-DLL liegt bei `sunny`. Das LAN-Profil ist Public.

## 2. Taskleisten-Symbol statt Menüleiste
Entscheid: unsichtbares Fenster plus `Shell_NotifyIcon`, `NOTIFYICON_VERSION_4`, Identität über `uID`, kein `guidItem`. Grund: `guidItem` hängt am Pfad der Datei und verlangt für Umzüge eine Signatur.

- Links- und Rechtsklick öffnen dasselbe Menü wie am Mac. Die Doku sagt nicht wörtlich, welche Nachricht bei welchem Klick kommt, darum behandelt der Code `NIN_SELECT`, `NIN_KEYSELECT`, `WM_CONTEXTMENU`.
- `SetForegroundWindow` vor `TrackPopupMenu`, `WM_NULL` danach, wie von Microsoft verlangt. `TaskbarCreated`: Symbol neu setzen, wenn der Explorer neu startet.
- **Menütexte teilen.** Die Logik aus `menuNeedsUpdate` wandert nach neu `orc/OrcMenu.{h,cpp}`, reines C++, beide Programme fragen dort. Sonst laufen die Status-Texte auseinander (Learning #51). `tools/orc_manual.py:82` liest heute `OrcApp.mm` und liest dann `OrcMenu.cpp`.
- Das Popup-Menü bleibt hell, auch unter dunklem Windows. Für dunkle Menüs habe ich keine dokumentierte API gefunden, undokumentierte nehme ich nicht.
- Ein neues Symbol landet laut Microsoft im Überlauf, nur der Nutzer holt es vor. Die Aussage ist für Windows 7 formuliert, für 11 steht sie nirgends. Ein Satz ins Handbuch.
- Eine Instanz: benannter Mutex. Ein zweiter Start öffnet die Einstellungen der ersten und endet. Am Mac verhindert das LaunchServices, unter Windows kämen sonst zwei ORC an USB, Port und `orc.pid`.
- Abmelden und Herunterfahren: Windows darf den Prozess nach `WM_ENDSESSION` jederzeit beenden. Der Abschluss aus `main()` wird eine Funktion, die der Handler aufruft, sonst bleiben UF1 und `orc.pid` hängen.

Symbol: Wortmarke aus `orc/OrcLogo.cpp`. Unter Windows ist das Feld quadratisch, bei 16 Pixeln bleiben rund 5 Pixel Buchstabenhöhe. Windows färbt nichts selbst, darum zwei Varianten, hell und dunkel, gewählt über `SystemUsesLightTheme`. Der Wert ist undokumentiert, fehlt er, gilt dunkel, das ist auch Microsofts eigene Beschreibung des Fehlfalls. `tools/orc_icon` lernt `.ico`, die Dateien liegen im Repo, weil das Werkzeug CoreGraphics braucht. `tools/orc-release.sh` rendert sie neu und bricht ab, wenn sie abweichen, damit es eine Zeichnung bleibt. CI am Mac ginge nicht: CoreGraphics kann je nach macOS andere Bytes liefern.

## 3. Fenster, WebView2, Laufzeit
Entscheid: WebView2 Evergreen, statisch gelinkter Loader (`WebView2LoaderStatic.lib`, keine zusätzliche DLL), Paket `Microsoft.Web.WebView2` 1.0.4314.35 per FetchContent mit Hash. Kein Fixed Version: wir müssten die Laufzeit mitliefern und aktuell halten.

- Laufzeit: Windows 11 hat sie immer, Windows 10 fast immer. Der Installer prüft `pv` in der Registry und ruft bei Bedarf den Bootstrapper mit `/silent /install`. Fehlt sie trotzdem beim Öffnen: kurze Meldung mit Link.
- Datenordner `%LOCALAPPDATA%\ORC\WebView2`, ausdrücklich gesetzt. Der Standard liegt neben der exe und scheitert unter Program Files.
- Hauptthread: STA mit Nachrichtenschleife, Pflicht laut Microsoft. Keine verschachtelte Schleife in einem WebView2-Callback, darum läuft der Logo-Dialog wie am Mac über eine Nachricht an das Fenster.

| Am Mac (`OrcWebWindow.mm`) | Unter Windows |
|---|---|
| `build`, 1280 x 820, Minimum 620 x 420 | `CreateWindowExW`, `WM_GETMINMAXINFO`, Per-Monitor-DPI im Manifest |
| dunkler Hintergrund, `alphaValue` | `WEBVIEW2_DEFAULT_BACKGROUND_COLOR`, sichtbar erst nach `NavigationCompleted`, dunkler Rahmen über `DWMWA_USE_IMMERSIVE_DARK_MODE` (ab Windows 11) |
| `windowShouldClose`: Feld übernehmen, Seite merken | `WM_CLOSE`, `ExecuteScript`, danach schliessen, dieselbe Zeile JavaScript |
| `windowWillClose`: Server stoppen | Controller schliessen, `set::server().stop()` |
| `webViewWebContentProcessDidTerminate` | `ProcessFailed`, neu laden |
| `createWebView…` (Open auf Studio Control) | `NewWindowRequested`, `Handled`, `ShellExecuteW` |
| `installEditMenu` | entfällt, Strg-C, V, X, A funktionieren |
| `SMAppService` | Abschnitt 9 |
| `NSOpenPanel` | `IFileOpenDialog`, dann `ph::importLogo` unverändert |

Einstellungen: `AreDevToolsEnabled` aus, `AreBrowserAcceleratorKeysEnabled` aus (F5, Strg-P, die Bearbeiten-Tasten bleiben laut Doku). `settings.html` läuft in Chromium schon, das wurde beim Umbau im Browser-Pane geprüft.

## 4. Pfade und Ressourcen
| Was | Mac | Windows |
|---|---|---|
| `supportDir()` | `~/Library/Application Support/ORC` | `%APPDATA%\ORC` |
| Ressourcen | `Contents/Resources/{moreme,settings}` | `<exe>\{moreme,settings}` |
| Log | `/tmp/orc.log` | `%TEMP%\orc.log`, `LogPath.cpp` kann das schon |
| REAPER-Import | `~/Library/.../REAPER/reaper-extstate.ini` | `%APPDATA%\REAPER\reaper-extstate.ini` |
| Version | `CFBundle` | `ORC_VERSION` aus derselben CMake-Zeile, dazu `ORC.rc` |

`resourceDir` wandert aus `PhonesServer.cpp` nach `OrcConfig`. `orc::supportDir()` und die drei Kopien in Rea-Sixtys `main.cpp` werden eine Funktion in `src/OrcHandover.h`.

**Umlaute im Kontonamen.** Der Code gibt überall schmale Pfade an `fopen`. Entscheid: `activeCodePage` UTF-8 im Manifest von ORC (ab Windows 10 1903, daher das Minimum) und `/utf-8` beim Compiler. Der Pfad kommt aus `GetEnvironmentVariableW` und wird nach `CP_ACP` gewandelt. So bekommt ORC UTF-8 und REAPER, dessen Prozess das Manifest nicht hat, seine ANSI-Seite, aus derselben Zeile. Ob `getenv` dem Manifest folgt, steht bei Microsoft nicht, darum kein `getenv`. Grenze: ein Kontoname ausserhalb der ANSI-Seite geht in Rea-Sixty nicht.

**Rechte.** `fchmod 0600` gibt es nicht. Am StoerPC erbt `AppData\Roaming` von `frank` Lesen für Users und Everyone (`icacls`). Darin lägen PIN, Hue- und OBS-Schlüssel. Entscheid: `%APPDATA%\ORC` bekommt beim Anlegen eine geschützte Liste nur für den aktuellen Benutzer.

## 5. Sockets
Neu `orc/OrcSock.h`, gilt für beide Server und die Tests: `sock_t`, Fehlercode, nicht blockierend, Schliessen, Poll, `winsock2.h` als Erstes (Memory `uf1-sidecar-rme-plan`). `SOCKET` ist unter Win64 kein `int`, also `std::map<sock_t, Conn>`.

- `WSAPoll` statt `poll`, nie `select`. `POLLIN` und `POLLOUT` sind erlaubt, `POLLPRI` wäre ein Fehler. Ein leeres Array gibt es nicht, weil der Code dort schon schläft (`PhonesServer.cpp:581`).
- `SO_NOSIGPIPE` entfällt, SIGPIPE gibt es nicht. `BCryptGenRandom` statt `arc4random`. `GetAdaptersAddresses` statt `getifaddrs`, die Adresse für den QR-Code nimmt Ethernet und WLAN vor VPN, wie heute `en*`.
- Kein `SO_REUSEADDR` und kein `SO_EXCLUSIVEADDRUSE` (Memory `windows-so-reuseaddr-hijacks-port`). Grenze: Ohne Option scheitert ein zweites Bind auf dieselbe Adresse, aber Wildcard neben einer bestimmten Adresse gelingt. Bindet ein anderes Programm `0.0.0.0:7006` und ORC `127.0.0.1:7006`, meldet Windows keinen Konflikt. Das ist kein neuer Fehler, nur eine Meldung „port in use“, die dort fehlt.
- `RmeManager`: `SIO_UDP_CONNRESET` aus. Sonst liefert `recv` nach jedem Senden an ein geschlossenes TotalMix `WSAECONNRESET`. Der Code läuft dann nur eine Runde leer, aber der Pfad ist unter Windows nie gefahren, weil das Side-Car dort aus ist (`reasixty_rmeConfigPath_` liefert leer).

Firewall: ein Programm, das eingehend lauscht, löst ohne Erlaubnis-Regel den Dialog aus. Admins bekommen bei Abbrechen eine Sperrregel, Nicht-Admins immer. MoreMe wäre damit beim ersten Einschalten dauerhaft taub, ohne dass der Nutzer es merkt. Entscheid: Der Installer legt eine Erlaubnis-Regel für `ORC.exe` an, das Deinstallieren entfernt sie. Sie gilt erst etwas, wenn MoreMe läuft, und MoreMe hat die PIN. Sie gilt für alle Profile, denn Windows macht unbekannte Netze zu Public. Für reines Loopback habe ich keine Microsoft-Aussage gefunden, mit der Regel ist die Frage für das installierte ORC egal.

## 6. WinUSB-Treiber
Entscheid: wiederverwenden, nicht nachbauen. `reasixty_installWinUsbDriver`, `…Uninstall…`, `…DriverStatus`, `winUsbRunElevated_` und der Job-Zustand (`main.cpp` ab 56940) ziehen nach neu `src/WinUsbDriver.{h,cpp}`, REAPER-frei, beide Programme rufen es. `main.cpp` darf nie in ORCs Quellenliste. INF und Skript bleiben eine Kopie, das eingebettete `winusb_inf.h` aus CMake auch.

- Die Geräteliste wird Parameter. Rea-Sixty bindet weiter UF8, UC1, UF1. ORC nur UF1 und UF8: Die UC1 kann ORC nicht, SSL 360 soll sie behalten (Memory `uf1-sidecar-rme-plan`, Punkt 7).
- Im Installer nicht. Grund: Der Treiber nimmt die Geräte SSL 360 weg, und er bindet nur, was gerade steckt. Das soll eine Handlung des Nutzers sein. Karte „USB driver“ auf dem Reiter General, nur unter Windows: pro Gerät der Status aus SetupAPI, Install, Remove. Vor Install ein Dialog mit einem Satz: SSL 360 sieht UF1 und UF8 dann nicht mehr.
- Ist ein Gerät da, aber nicht auf WinUSB, liefert libusb `NOT_SUPPORTED`. Die Menüzeile sagt dann „UF1   needs the USB driver“. Status nur beim Öffnen des Menüs, nie aus dem USB-Thread (Memory: nicht aus dem Input-Thread pollen). Der Text kommt in die Handbuch-Prüfung.
- Hat Rea-Sixty den Treiber schon gesetzt, gilt er auch für ORC, die Prüfung fragt nur nach dem Dienst `WinUSB`. Umgekehrt ebenso. Remove trifft beide Programme, das steht im Dialog.
- Deinstallieren von ORC lässt den Treiber in Ruhe.
- Ein Gerät, das erst nach der Treiber-Installation zum ersten Mal angesteckt wird, kann über die normale Rangfolge auf SSLBUS landen (Kommentar im Skript). Das bleibt so und steht im Handbuch.

WinUSB erlaubt ein Programm pro Gerät. Hotplug von libusb ist unter Windows ab Werk aus, ORC braucht es nicht, es versucht neu zu öffnen (kein Treffer für `hotplug` im Code).

## 7. TotalMix und OSC
Gleicher Weg wie am Mac: UDP auf `127.0.0.1`, Global OSC ab TotalMix 2.1, zwei Remotes (Flächen 7005/7006, MoreMe 7003/7004). `host` muss IPv4 sein (`RmeManager.cpp:618`), `localhost` wird zu `127.0.0.1`. RMEs Handbuch beschreibt die vier Remote-Seiten für Mac und Windows gleich. Am 30.09. hörte TotalMix unter Windows auf `0.0.0.0` (IPv4), gemessen.

**Bekannte Falle, gemessen am 30.09. auf dem StoerPC:** Steht in TotalMix unter OSC Details „Send changes“ aus, beantwortet es `/sendall` nur mit Pegeln. ORC wäre dann online, mit leeren Kanälen. Das kommt ins Handbuch, keine neue Menüzeile.

## 8. Übergabe ORC und Rea-Sixty
Dieselben Dateien, derselbe Ablauf (`src/OrcHandover.h`), nur in `%APPDATA%\ORC`. WinUSB macht die Übergabe sogar Pflicht, ein zweites Öffnen scheitert. Dazu kommt:

- `kill(pid, 0)` wird `orc_handover::processAlive`: `OpenProcess` plus `GetExitCodeProcess`. `ACCESS_DENIED` zählt als lebt, wie `EPERM` heute (REAPER kann erhöht laufen). Windows vergibt PIDs schnell neu, eine fremde PID hielte ORC zurück, bis der Prozess endet. Am Mac ist das dieselbe Grenze, ich baue nichts dagegen.
- `rename` über eine bestehende Datei scheitert unter Windows. `writeRmeJson` geht über `MoveFileExW` mit `REPLACE_EXISTING`. `fopen` öffnet ohne `FILE_SHARE_DELETE`, ein Leser kann das Ersetzen und auch `std::remove` scheitern lassen. Rea-Sixty liest `rme.json` einmal pro Sekunde, die Übergabe prüft zweimal pro Sekunde. Darum `replaceFile` und `removeFile` mit kurzem Wiederholen, und ein Test mit Schreibern und Lesern. Sonst bliebe `handover` still liegen.
- `stat` liefert unter Windows Sekunden. `Bindings.cpp:5195` und `main.cpp` (`reasixty_rmeLoadConfig_`) vergleichen Zeit und Grösse, zwei Änderungen in derselben Sekunde mit gleicher Grösse gingen verloren. Gemeinsamer Helfer mit `GetFileAttributesExA`.
- Rea-Sixty: die 9 `__APPLE__`-Stellen, beide Pfade und der Schalter „Take the UF1 over from ORC“ gelten auch für `_WIN32`. Damit wird auch das RME-Side-Car in Rea-Sixty unter Windows erstmals aktiv, denn es hängt an denselben Pfaden. Das ist die grösste Folge dieses Abschnitts: Rea-Sixty braucht dafür ein eigenes Release.
- Ein Rea-Sixty ohne diese Stufe kennt ORC unter Windows nicht. Dann gilt: wer zuerst öffnet, hat das Gerät, der andere zeigt „not open“.

## 9. Autostart
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, Wert `ORC`, ab Werk aus, geschaltet im Prozess ohne Rechte. Nur für die Kopie unter `%ProgramFiles%\ORC`, sonst grau, aus demselben Grund wie am Mac (Anmeldeobjekt zeigte auf Build-Ordner, 16.10.). Der Tooltip „Only for the ORC in Applications“ kommt aus dem Modell, je Plattform ein Text.

Grenze: Schaltet der Nutzer ORC im Task-Manager aus, bleibt der Run-Wert stehen und die Seite zeigt weiter An. Das sagt nur der undokumentierte Schlüssel `StartupApproved\Run`, und die Quellen sind sich über dessen Inhalt nicht einig. Ich lese ihn nicht. Ein Satz im Handbuch.

## 10. QR im Menü
Windows hat keinen eingebauten QR-Generator. Entscheid: nayuki QR-Code-generator (MIT, eine `.cpp`, eine `.hpp`) per FetchContent auf festem Commit, `vendor/` ist gitignored. Pixelgenau gezeichnet in einem Owner-Draw-Eintrag (`WM_DRAWITEM`), statt des Bilds über `MIIM_BITMAP` aus dem ersten Plan: Windows hätte das Bild beim Darüberfahren eingefärbt. Weisser Rand wie am Mac, nur Adresse, nie PIN. Der Mac bleibt bei CoreImage. Adresse kopieren: `SetClipboardData`.

## 11. Tastatur-Shift
`OrcKeyboard.h`, zweiter Zweig: `GetAsyncKeyState(VK_SHIFT) & 0x8000`, nur im Moment des Drucks, wie am Mac. Grenze laut Microsoft: Hat ein erhöhtes Programm den Vordergrund, kommt 0, dann wirkt Shift nicht. Der Rest geht.

## 12. Weitere Fallen im eigenen Code
Alle hätten den Mac-Test bestanden.

- **Absturz beim Start:** `setvbuf(stdout, nullptr, _IOLBF, 0)` in `main.cpp:88`. Laut Microsoft ist Grösse 0 mit `_IOLBF` ein ungültiger Parameter, das beendet den Prozess. Unter Windows `_IONBF`. `logLine` macht `fflush` ohnehin.
- **Pfad über die Datei:** `PhonesServer.cpp` prüft den Logonamen aus `moreme.json` nur auf `/`. Unter Windows käme `..\..\x` durch. Auch `\` und `:` prüfen.
- **Stack:** `-Wframe-larger-than` hängt nur an `reaper_uf8` (CMake ab 503), `Config` ist rund 937 KB. Das Mac-Ziel `orc` und die Tests bekommen dieselbe Sperre, `ORC.exe` zusätzlich `/STACK` auf 8 MB (Memory `windows-stack-overflow-config`).
- `std::snprintf` steht 12-mal in ORC-Dateien, WDL macht es unter Windows zum Makro. CI zeigt jede Stelle.
- `NOMINMAX` gilt nur für `reaper_uf8`, kommt zu `orc`. `/MT` für `ORC.exe`, damit kein VC-Redistributable nötig ist. Die fertige `libusb-1.0.dll` importiert `VCRUNTIME140.dll` (`dumpbin` am StoerPC), darum liegt `vcruntime140.dll` neben `ORC.exe` im Installer.
- `this Mac` steht in Fehlertexten und Handbuch, wird `this computer`, auch am Mac.

**Beim Bau gefunden, stand nicht im Plan:** Die Namen der Snapshots und Layouts kommen aus `last.*.xml` von TotalMix (`RmeNames.cpp`), bisher nur am Mac. Unter Windows liegt die Datei in `%LOCALAPPDATA%\TotalMixFX` (am StoerPC gelesen). `namesFromDisk` liest jetzt beide. WDL macht `snprintf` unter Windows zu einem Makro ohne Rückgabewert, darum auch keine Auswertung davon. Eine schlanke `windows.h` hat kein `timeval`, `UF1Device` und `UF8Device` holen es aus `winsock2.h`.

## 13. Installer und Signierung
**Installer:** Erhöht, nach `%ProgramFiles%\ORC`, mit `ORC.exe`, `libusb-1.0.dll`, `moreme\`, `settings\`, `Licenses\`. Schliesst ein laufendes ORC per Mutex, setzt die Firewall-Regel, zieht bei Bedarf die WebView2-Laufzeit. `%APPDATA%\ORC` bleibt beim Deinstallieren liegen, wie am Mac. Werkzeug: NSIS, zlib und frei, CI lädt es nach. Inno Setup bittet kommerzielle Nutzer um eine bezahlte Lizenz.

**Microsoft Store: verworfen.** Das schliesst den offenen Punkt aus `orc-standalone`. MSIX leitet neue Dateien unter `AppData` in einen privaten Ordner um, Rea-Sixty fände `%APPDATA%\ORC` nicht mehr. MSIX kennt keine Treiber, der Store nimmt keine Programme mit erhöhten Rechten, eine exe müsste schon signiert sein.

**Signatur, unser Stand:** Rea-Sixty ist unsigniert. Smart App Control blockt dann, wenn der Dienst nichts vorhersagen kann, unsignierte Programme, also auch den ORC-Installer und `ORC.exe`. Abschalten lässt es sich laut Microsoft inzwischen ohne Neuinstallation, ausser die Diagnosedaten sind aus. SmartScreen warnt ohne Signatur bei jeder Version neu. Eine gültige Signatur lässt Smart App Control durch, SmartScreen baut Ruf trotzdem erst auf, auch EV hilft nicht mehr.

| Weg | Kosten | Haken |
|---|---|---|
| Unsigniert wie Rea-Sixty | keine | SmartScreen-Warnung, Known Issue Smart App Control |
| SignPath Foundation | gratis | OSI-Lizenz, automatischer Build aus dem Repo, MFA, Signaturregeln auf der Website, Ruf des Projekts. Prüfung offen. Mein Lesen: passt, weil Quelle MIT ist |
| Certum Open Source | ab 69 Euro | auf Frank als Person, Karte und Leser, signieren nur dort, wo die Karte steckt, nicht in CI. Seite zeigt zugleich „out of stock“ und „in stock“ |
| Azure Artifact Signing | rund 9.99 USD im Monat | Microsofts Seiten widersprechen sich zu Schweiz und zu 3 Jahren Steuerhistorie, Einzelfirma ungeklärt. Vor dem Anlegen Support fragen |

Entschieden: erste Fassung unsigniert mit demselben Known Issue, parallel SignPath, das würde auch Rea-Sixty helfen. Der Treiber-Katalog signiert sich weiter selbst, das ist davon unabhängig.

## 14. CI und Release
- `extension/CMakeLists.txt`: Ziel `orc` unter `if(APPLE OR WIN32)`, geteilte Liste mit `win_http.cpp` statt `macos_http.mm`, Windows-Quellen, `ws2_32`, `iphlpapi`, `bcrypt`, `winhttp`, `setupapi`. libusb wie bei der DLL (`LIBUSB_ROOT`), keine hidapi. Die Version zieht aus dem Block `if(APPLE)` heraus, `orc-release.sh:26` liest sie weiter mit `sed`.
- Eigener Workflow `.github/workflows/orc-windows.yml` statt eines Schritts in `build.yml`, Schalter `-DORC_WINDOWS=ON`: Ein rotes ORC hält die Rea-Sixty-DLL nicht auf, und ein Testlauf belegt keine Mac-Runner. Baut `orc`, fährt `ctest -C Release -R` auf den ORC-Tests, baut den Installer, lädt ihn als Artefakt hoch. Hashes für libusb, WebView2, QR.
- Bauen in CI, wie die Rea-Sixty-DLL. Der StoerPC hat keine CMake im Pfad und braucht auch keine. Die Notarisierung bleibt lokal am Mac. Release: `orc-release.sh` wie bisher, das Windows-Artefakt desselben Commits kommt in dasselbe GitHub-Release.
- `licenses/THIRD-PARTY-NOTICES.txt`: libusb-Absatz je Plattform (DLL neben der exe, austauschbar, LGPL), dazu WebView2 SDK (BSD-artig), QR (MIT). Der libusb-Tarball liegt schon im Release.

## 15. Tests
Bestehende Tests, die auf Windows laufen sollen: `orc_handover`, `orc_websocket`, `orc_phones`, `orc_phones_server` (mit `FakeMix`), `orc_settings`, `orc_keys`, `rme_osc`, `rme_input`, `rme_softkeys`, `rme_face`. Sie brauchen neu `tests/TestScratch.h` statt `mkdtemp`, `setenv`, `dirent`, und `OrcSock.h`. Sie setzen `HOME` oder `APPDATA` auf einen Scratch-Ordner.

Neue Tests:
- `replaceFile` und `removeFile` gegen offene Leser
- Scratch-Ordner mit Umlaut für `orc.json`, `rme.json`, `moreme.json`
- Logoname mit `\` wird abgelehnt
- UDP-Link nach Senden an einen geschlossenen Port
- `processAlive` für lebend, tot, unbekannt
- Rechte von `%APPDATA%\ORC`
- Reiter-Texte je Plattform

Regel wie immer: nur Ports vom System, nie 7001 bis 7006, kein USB, nichts öffnet ein Gerät. Tests laufen in CI und per SSH am StoerPC unter `claude`, nie am Mac gegen echte Ports. Die Seite läuft weiter in WebKit und Chromium gegen den echten Server.

## 16. Stufen
Jede Stufe baut auf beiden Plattformen, die Mac-Tests bleiben grün, das Mac-Verhalten ändert sich nicht.

**Stufe 1, Fundament ohne Oberfläche.** `OrcSock.h`, Pfade, Log, `OrcHandover`-Helfer, `OrcMenu` (Mac benutzt es), `resourceDir`, Version, Fallen aus Abschnitt 12, `TestScratch.h`. CMake baut unter Windows eine Testbibliothek, CI fährt die Tests aus Abschnitt 15. Läuft: `SettingsServer` und `PhonesServer` unter Windows, gegen `FakeMix`.

**Stufe 2, ORC ohne Gesicht.** `ORC.exe` als Konsolenprogramm. `main` und `Surface` laufen, `runApp` wartet nur auf Strg-C. Am Gerät: UF1, UF8, TotalMix, MoreMe im Browser, alles im Log. Das ist der erste Test für Frank und der erste Lauf des `RmeManager` unter Windows.

**Stufe 3, Taskleiste.** `OrcTray.cpp`, Symbole, Menü, QR, Shift, Mutex, `WM_ENDSESSION`, kein Konsolenfenster mehr. `Settings…` öffnet die Seite vorerst im Standardbrowser, das geht dank Token.

**Stufe 4, Fenster.** `OrcWebWindow.cpp`, WebView2, Logo-Dialog, Open at login, Rechte des Ordners.

**Stufe 5, Treiber.** `src/WinUsbDriver`, Rea-Sixty ruft denselben Code, Karte auf General, Menüzeile. Am StoerPC stehen die Geräte schon auf WinUSB, Prüfung über Remove und Install.

**Stufe 6, Übergabe.** Rea-Sixty-Seite, Side-Car unter Windows, `stat`-Helfer. Test: ORC läuft, REAPER startet, Marke, Quittung, REAPER beendet, ORC übernimmt, Hue- und DynaMount-Werte gehen mit.

**Stufe 7, Installer und CI-Artefakt.** Installer, Firewall-Regel, Bootstrapper, Lizenzen.

**Stufe 8, Texte.** Handbuch: Voraussetzungen, Treiber, Firewall, Überlauf, Send changes, Grenzen. `orc_manual.py --check` deckt die neuen Status-Texte ab. Website `orc.astro` („Mac only“), ORC-README. Known Issues. Danach Franks Test als Kunde, Release erst auf sein Wort.

Arbeit auf dem Branch bis nach Franks Test, nichts nach `main`.

## 17. Was nur Frank am StoerPC testen kann
Ich kann per SSH Tests fahren und Dateien ablegen, aber kein Fenster sehen und nichts bedienen.

- Taskleiste und Menü, heller und dunkler Modus, Überlauf, QR scannen
- Fenster: Darstellung, Kopieren, Dialoge, Studio Control im Reiter
- UF1 und UF8 am Gerät, Fader, Lampen, Sleep
- TotalMix mit UFX+, Send changes, MoreMe vom Handy durch die Firewall
- Treiber: Rechteprompt (nie ausgelöst, Memory `HANDOFF-winusb-installer-visibility`), Remove und Install mit Gerät
- Übergabe mit REAPER, Autostart bei echter Anmeldung, Herunterfahren
- Installer als Kunde, SmartScreen, Smart App Control ein

## 18. Was ich nicht baue
Windows auf ARM (auch Rea-Sixty hat kein ARM-Build), Microsoft Store, Auto-Update, dunkle Popup-Menüs über undokumentierte Aufrufe, portables REAPER beim Import, neue Einstellungen, Hilfesätze auf der Seite. Der Dialog vor Treiber-Install ist die eine Warnung, die sein muss.

## 19. Entschieden (Frank 03.11.)
1. **Signatur:** erste Fassung unsigniert wie Rea-Sixty, mit demselben Known Issue. SignPath wird parallel beantragt, das macht Frank: Der Antrag geht nach aussen und verlangt Signaturregeln auf der Website.
2. **Symbol:** Wortmarke. Frank hat keine Änderung verlangt, der Plan bleibt stehen.
3. **Installer:** NSIS.
4. **Rea-Sixty:** Side-Car unter Windows mit eigenem Rea-Sixty-Release.
5. **Version:** Mac und Windows gemeinsam als nächste ORC-Version aus einem Commit.
6. **Firewall:** Regel für alle Netzprofile.
7. **Gerätename:** neutral „SSL UF1 (WinUSB)“, Dateiname und Zertifikat bleiben.
8. **Send changes:** nur Handbuch, keine neue Menüzeile. Frank hat dazu keine Empfehlung bekommen, darum gilt der kleinere Weg.
9. **Mergen:** Branch bis nach Franks Test. Auch hier keine Empfehlung, darum der Weg, der `main` nicht anfasst.
10. **Testaufbau:** Frank testet als `sunny`. Offen bleibt nur, dass die Geräte für Stufe 2 angesteckt sind und TotalMix 2.1 oder neuer läuft.

## 20. Belegt und nicht belegt
**Aus dem Code gelesen:** alle `datei:zeile` und Tabellen oben.

**Am StoerPC gemessen 03.11.:** Windows-Version, WebView2-Version, Smart App Control, Gerätebindung, Treiberpakete, TotalMix-Pfad und -Version, offene Ports, Konten, Rechte auf `Roaming`, Netzprofil. **Gemessen 30.09.** (Memory): Send changes, TotalMix auf `0.0.0.0`.

**Online belegt, Primärquelle gelesen** (Liste unten): WebView2 Datenordner, statischer Loader, Lizenz, Evergreen, Erkennung, Bootstrapper, STA, Einstellungen, Ereignisse, NuGet-Version. `Shell_NotifyIcon`, `TaskbarCreated`, `guidItem`, `TrackPopupMenu`, `MIIM_BITMAP`, `DWMWA_USE_IMMERSIVE_DARK_MODE`, `WM_ENDSESSION`, `CreateMutex`. `WSAPoll`, `SIO_UDP_CONNRESET`, Bind-Tabelle, Firewall-Dialog, `setvbuf`, `MoveFileExW`, UTF-8-Manifest, `GetAdaptersAddresses`, `BCryptGenRandom`, `GetAsyncKeyState`, `OpenProcess`. Run-Schlüssel, Smart App Control, SmartScreen, Artifact Signing, SignPath, Certum, MSIX, Store, Inno, NSIS, libusb, RME-Handbuch, nayuki.

**Online nicht belegt:**
- Welche Klick-Nachricht genau kommt (Code behandelt alle)
- Überlauf unter Windows 11 (Doku ist für Windows 7 formuliert)
- Dunkle Popup-Menüs: keine API gefunden
- `SystemUsesLightTheme`: nur ein Spec-Issue von Microsoft nennt es undokumentiert
- `getenv` unter UTF-8-Manifest
- `StartupApproved`
- Firewall und Loopback
- Verhalten von `MoveFileExW` bei offenem Ziel, Microsofts Seite schweigt, die Treiber-Doku sagt, es scheitere ohne POSIX-Semantik. Darum der Test
- `fopen` ohne `FILE_SHARE_DELETE`: nur aus einem inoffiziellen UCRT-Spiegel
- Einzelfirma bei Artifact Signing, Inno-Preise, ob TotalMix ohne Interface OSC spricht

**Aus dem Gedächtnis, vor dem Bau gegen die Doku prüfen:**
- `/STACK` und `/MT` als Linker- und Compiler-Schalter
- `IFileOpenDialog`, `SetClipboardData`, `ShellExecuteW`, `WM_GETMINMAXINFO`, `GetFileAttributesExA`
- Syntax von `netsh advfirewall`
- NSIS-Paket per Chocolatey in CI, NSIS-Mutex
- Geschützte Rechteliste beim Anlegen des Ordners

## Quellen
WebView2: [Datenordner](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder), [statisch linken](https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/static), [Verteilung](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution), [Evergreen](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/evergreen-vs-fixed-version), [Threading](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model), [Settings](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2settings), [Settings3](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2settings3), [ICoreWebView2](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2), [Controller2](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2controller2), [NuGet](https://www.nuget.org/packages/Microsoft.Web.WebView2), [Lizenz](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4314.35/License).

Taskleiste und Fenster: [Shell_NotifyIcon](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw), [NOTIFYICONDATA](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw), [TaskbarCreated](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar), [TrackPopupMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu), [MENUITEMINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-menuiteminfow), [Überlauf](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area), [DWM](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute), [Themes-Issue](https://github.com/microsoft/WindowsAppSDK/issues/6890), [WM_ENDSESSION](https://learn.microsoft.com/en-us/windows/win32/shutdown/wm-endsession), [CreateMutex](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw).

Sockets und Dateien: [WSAPoll](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsapoll), [recvfrom](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-recvfrom), [Winsock IOCTLs](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls), [SO_REUSEADDR](https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse), [Firewall-Regeln](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/rules), [setvbuf](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setvbuf), [_fsopen](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fsopen-wfsopen), [rename](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename), [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw), [FILE_RENAME_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information), [UTF-8](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page), [GetAdaptersAddresses](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses), [BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom), [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate), [OpenProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess), [GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess).

Autostart, Installer, Signatur: [Run-Schlüssel](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys), [Smart App Control FAQ](https://support.microsoft.com/en-us/topic/what-is-smart-app-control-285ea03d-fa88-4d56-882e-6698afdb7003), [Smart App Control für Entwickler](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview), [SmartScreen-Ruf](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation), [Signaturwege](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options), [Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart), [MSIX-Signatur](https://learn.microsoft.com/en-us/windows/msix/package/signing-package-overview), [SignPath](https://signpath.org/terms), [Certum](https://shop.certum.eu/open-source-code-signing.html), [MSIX hinter den Kulissen](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes), [MSIX vorbereiten](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-prepare), [Store-Anforderungen](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements), [Inno-Lizenz](https://jrsoftware.org/isorder.php), [Inno AppMutex](https://jrsoftware.org/ishelp/topic_setup_appmutex.htm), [NSIS-Lizenz](https://nsis.sourceforge.io/License), [Runner-Image](https://raw.githubusercontent.com/actions/runner-images/main/images/windows/Windows2025-Readme.md).

libusb, RME, QR: [libusb Windows](https://github.com/libusb/libusb/wiki/Windows), [libusb ChangeLog](https://raw.githubusercontent.com/libusb/libusb/master/ChangeLog), [configure.ac](https://raw.githubusercontent.com/libusb/libusb/v1.0.30/configure.ac), [windows_common.c](https://raw.githubusercontent.com/libusb/libusb/v1.0.30/libusb/os/windows_common.c), [RME UFX III Handbuch](https://www.rme-audio.de/downloads/fface_ufx3_e.pdf), [nayuki](https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/Readme.markdown).
