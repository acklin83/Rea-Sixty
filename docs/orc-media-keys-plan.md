# ORC: Medientasten als Built-ins

Stand 15.11.2026, gebaut (siehe „Entscheide und Bau“ unten). Anlass: Forenpost (creal, JRiver/Spotify ohne REAPER).
Frank 15.11.: „anzeige brauchen wir nicht. nur built-ins für orc“.

## Was es tut

ORC bekommt Aktionen, die die Medientasten der Tastatur drücken. Das System gibt sie an das
Programm, das gerade spielt (Spotify, Music, JRiver, Browser). Man legt sie im ORC-Fenster auf
eine Transporttaste, einen Soft-Key oder eine UF8-Taste. Startet REAPER, übernimmt Rea-Sixty die
Fläche wie heute, und die Transporttasten sind wieder REAPERs.

## Die Aktionen (Vorschlag, Frage 1)

| Name | Titel | Kurzlabel (8) | Mac `NX_KEYTYPE_*` | Windows `VK_*` |
|---|---|---|---|---|
| `media_play_pause` | Play/Pause | Play | `PLAY` (16) | `VK_MEDIA_PLAY_PAUSE` |
| `media_next` | Next track | Next | `NEXT` (17) | `VK_MEDIA_NEXT_TRACK` |
| `media_previous` | Previous track | Prev | `PREVIOUS` (18) | `VK_MEDIA_PREV_TRACK` |

Alle `BuiltinKind::Once` (kein Zustand, keine Toggle/Press-Wahl). Feuern nur beim Drücken.

## Dateien

1. **`orc/OrcMedia.h`** (neu), Form wie `OrcKeyboard.h` / `OrcLight.h`: eine Tabelle Name, Titel,
   Mac-Code, Windows-Code; `orc::postMediaKey(code)` (Mac: `NSEvent` systemDefined, Subtyp 8,
   Drücken + Loslassen, `CGEventPost(kCGHIDEventTap)`; Windows: `SendInput` Drücken + Loslassen);
   `orc::registerMediaBuiltins(post)`. Der Mac-Teil braucht Objective-C: eigene `orc/OrcMedia.mm`
   oder `CGEventCreate` + Felder in C (prüfen, was ohne .mm geht).
2. **`orc/Surface.cpp`** neben `registerBrightnessBuiltins`: `orc::registerMediaBuiltins(orc::postMediaKey)`.
3. **Dieselbe Tabelle an den drei Stellen, die heute `sleep_now` und die Helligkeit nachmelden**:
   `tools/orc_manual_dump.cpp`, `tools/orc_settings_demo.cpp`, `tests/test_orc_settings.cpp`
   (mit leerem `post`), damit Handbuch und Demo zeigen, was das Fenster anbietet.
4. **`src/Bindings.cpp`**: Beschreibung (`kBuiltinDocs`), Kurzlabel (`kBuiltinLabels`), Kategorie
   `"Media"` in `builtinCategory()` und `builtinCategoryOrder()`.
5. **Die Regel „welche Aktionen bietet ORC an“ steht zweimal**: `orc/OrcSettingsModel.cpp:683`
   (`RME` oder `Brightness`) und `tools/orc_manual_dump.cpp:160` (dasselbe abgeschrieben).
   Eine Funktion daraus machen, beide rufen sie, `Media` dazu.
6. **`tools/check_builtin_docs.py`** liest nur `src/`. Ohne Anpassung sieht es die Namen aus
   `orc/OrcMedia.h` nicht. Datei bzw. Tabelle in seine Liste.
7. **CMake**: `OrcMedia.mm` (falls nötig) ins ORC-Target, Mac: AppKit ist schon gelinkt (prüfen).
   Windows-Ziel `orc/win` mitnehmen.
8. **Handbuch** `orc/manual/template.html`: die Aktionsliste kommt aus dem Dump; ein Satz, was
   Medientasten tun, und der Hinweis zur Mac-Freigabe (Frage 3).
9. **Tests**: `test_orc_settings`: Picker bietet die drei an, Tasten lassen sich belegen;
   Tabelle Name zu Code auf beiden Plattformen.

## Wer liest die Bindung sonst noch

- **`bnd::orcKeySlot` (Bindings.cpp:5280) bleibt bei `rme_`.** Die Funktion entscheidet zweierlei:
  ORCs Transportlampe (`orc/Surface.cpp:381`) UND ob Rea-Sixty im Side-Car die Taste ORC
  überlässt (`dispatchSideCarKey`, Lampe `main.cpp:37142`). Nähme sie `media_` mit, würde
  Rea-Sixty bei laufendem REAPER die Transporttaste behalten und ein Built-in feuern, das es
  nicht kennt: REAPERs Play wäre weg. So wie sie ist, ergibt sie genau das Umschalten, das creal
  will.
- Folge: In ORC bleibt eine Transporttaste mit Medien-Aktion **dunkel** (Lampenregel fragt
  `orcKeySlot`). Soll sie leuchten, braucht die Lampe eine eigene Regel „ORC führt es aus“,
  getrennt von „ORC nimmt es REAPER weg“ (Frage 2).
- Soft-Keys der RME-Bank: In ORC feuern sie. Im Rea-Sixty-Side-Car liest Rea-Sixty dieselbe Bank
  aus orc.json, kennt `media_*` nicht und tut nichts. Die Zelle zeigt dort trotzdem ihr Label
  (prüfen, wie `RmeSoftKeys::row` ein unbekanntes Built-in malt).
- UF8 in ORC: `Uf8Surface` nimmt dieselbe Aktionsliste, also auch die drei.

## Mac: Freigabe (nicht geprüft)

Ob `CGEventPost` für Medientasten die Freigabe unter Bedienungshilfen braucht, sagen die Quellen
nicht einheitlich. Messen: signierte ORC aus `/Applications`, Taste drücken, Spotify beobachten.
Frank testet. ⚠ Die Entwicklungsfassung ist ad hoc signiert, eine Freigabe hält dort nicht über
den nächsten Build. ORC ist nicht in der Sandbox, Hardened Runtime verbietet das Senden nicht
(prüfen).

## Nicht in diesem Plan

Anzeige von Titel/Interpret, Spotify-Web-API, JRiver MCWS, Systemlautstärke, Rea-Sixty-Seite
(REAPER hat eigene Wege), Werksbelegung (Frage 4).

## Fragen an Frank

1. Nur Play/Pause, Next, Previous, oder auch Lautstärke +/− und Mute des Systems?
2. Transporttaste mit Medien-Aktion in ORC: dunkel lassen oder in der gewählten Farbe leuchten?
3. Mac ohne Freigabe: beim ersten Druck die macOS-Frage auslösen, oder still nichts tun und es
   nur im Handbuch erklären?
4. Werksbelegung: keine (Nutzer legt selbst), oder z.B. Play/Stop/FFW/RWD in ORC ab Werk auf
   Medien, solange REAPER nicht läuft?

## Entscheide und Bau (15.11.2026)

Frank: „1. alle und Lautstärke auch auf jog wheel, 2 ja, 3 auslösen, 4 ja“.

- Sechs Aktionen: `media_play_pause`, `media_next`, `media_previous`, `media_volume_up`,
  `media_volume_down`, `media_mute` (Kategorie Media, Labels Play/Next/Prev/Vol +/Vol -/Sys Mute).
- Jog: neues Ziel `system` („System volume“) nur in der Jog-Liste (`jogChoices`), 1 % pro Raste,
  SHIFT fein. Der UF8-CHANNEL-Encoder folgt dem Jog-Ziel. Mac über Core Audio (virtuelle
  Hauptlautstärke, AudioObject*), Windows über `IAudioEndpointVolume`; Drehen hebt Stumm auf.
  `rmeu::resolveTarget` kennt `system` nicht: Zeitfeld leer, Rea-Sixtys Side-Car tut nichts.
- Lampe: ORCs Transportlampe fragt jetzt `orc::runsSlot` (dieselbe Regel wie der UF8,
  `orc/OrcActions.h`), nicht mehr `orcKeySlot`. Damit leuchten auch Sleep/Helligkeit auf einer
  Transporttaste (vorher feuerten sie dunkel).
- Mac-Freigabe: erster Druck ohne Recht ruft `CGRequestPostEventAccess()`, einmal pro Lauf.
- Werk: PLAY/FFW/RWD auf Play/Pause, Next, Previous (`seedTransportKeys`, nur über die geerbte
  Werksschicht, alle drei Layer, Plain).
- Eine Regel für die Aktionsliste: `orc::offersAction` (Seite und Handbuch-Dump).
