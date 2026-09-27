# Session 26./27.09.2026: UF1-Einschalten, Fader, ORC-Log, RME-Start

Fortsetzung von `session-2026-09-25-rme-banks-orc.md`. Alles auf main, CI grün
bis `3cb4fc0`; `164302e` gerade gepusht.

## Gebaut

| Commit | Was |
|---|---|
| `335ed92` | UF1 nach Aus/Ein: der erste Kontakt nach dem Einschalten lässt `FF 01` unbeantwortet (Trace: 22 Timeouts), der zweite antwortet nach 217 ms. `runInit_` wartet 500 ms auf die Antwort (`awake()`), sonst `needsReopen`. `close()` vergisst die Sitzung (Queue, held, Zyklus, LED-Tabelle). Timing war es nicht (3 s Wartezeit änderte nichts). |
| `f4ca890`, `e556b8d` | Kein Fader-Tanz beim Verbinden, UF1 und UF8: Motor-Frames `FF 1D`/`FF 1E` samt Pausen im Init übersprungen. Von Frank am Gerät bestätigt. |
| `77f0251` | ORC loggt immer nach `orc.log` (Log-Ordner, >10 MB → `.1`). `ORC_TRACE=1` schreibt zusätzlich `rme_osc_trace.log`. |
| `5f934c7`, `3cb4fc0` | Fader-Hüpfer im RME-Side-Car (nach Hand + Jog, dann nach Loslassen): Handposition beim Ziehen als Motorziel mitschicken, wie SSL 360 (cap53/cap132) und Rea-Sixtys Kanal-Maler („wobble“); beim Loslassen Ziel, dann an. Frank: „kein hüpfer mehr“. |
| `7426edc` | TotalMix-EQ: Shelves/Band-Filter = RBJ-Cookbook mit Q (Pixel-Fit an Franks Screenshots). |
| `026fd27`, `dac3178`, `0585e56`, `39ff571` | Pegel stoppen bei -64.5 dB, ein Schritt tiefer = aus, angezeigt als „-“ (auch STRIP). |
| `164302e` | UF1-Startansicht „RME“ (Settings → Behaviour → UF1), nur mit ORC. **Am Gerät noch ungeprüft.** |

Zurückgenommen, nie committet: ein Devices-Dropdown „Stays with ORC“ (Missverständnis, Frank wollte die RME-Startansicht).

## Forum (cockos, Thread Rea-Sixty, Seite 12)

- Meter Pro leer auf dem UF1 bei creal (#472) und „SSL“ (#477), beide Win 11, SSL 360 2.1.12 / Meter Pro 1.3.7 (= Franks Stand). Vermutung, ungeprüft: SSL 360 läuft im Hintergrund und bekommt die Plug-in-Streams; Rea-Sixty fällt auf REAPER-Pegel zurück (`main.cpp:27876`), darum geht nur die VU. Antwort mit Bitte um `[sslcore]`-Zeilen aus `%TEMP%\rea_sixty.log` ist formuliert.
- „Fenster schliesst bei SOFT/Kreuz-Taste“ (creal): genau diese beiden blenden eine Zeile über den Editor-Spalten ein (`SettingsScreen.cpp:7926/7946`); die Spalten sind korrekt gepaart. Offen, braucht REAPER/ReaImGui-Version.
- sebsteeno (#480): Antwort formuliert. Offen und von Frank nicht entschieden: Motor beim Beenden von REAPER nicht losgelassen (`shutdownDevices_` schickt kein Motor aus), Jog zwischen Takten, 0.9 dB am Fader-0, Automation in Read, Reverse bei Stufenparametern.

## Offen

- Frank testet die RME-Startansicht (heute Nachmittag).
- RME-Label auf SHIFT+MODE (25.09.): seit `dd0133f` nicht mehr gemeldet, nicht bestätigt.
- Tote Zweige `g_uf1BankSet` in `SettingsScreen.cpp` (Aufgabe vorgeschlagen).
