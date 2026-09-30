# Fixed Item Lanes auf UF8 und UF1: Plan

Stand 30.09.2026. Nichts davon ist gebaut. Recherche in drei Teilen (REAPER-API und Actions,
andere DAWs und Surfaces, unsere eigene Mechanik); Quellen stehen unten. Was als Tatsache über
REAPER dasteht, ist gelesen (SDK-Header, ReaScript-Doku 7.81, User Guide 7.81 Kap. 8.12,
whatsnew.txt, Action-Liste). Was nur vermutet ist, steht unter „Zuerst messen“.

---

## 1. Was Fixed Lanes sind, und wofür man sie braucht

Eine Spur mit `I_FREEMODE = 2` hat übereinanderliegende Lanes, „Spuren in der Spur“. Items
liegen in einer Lane (`I_FIXEDLANE`), jede Lane spielt exklusiv, mit anderen zusammen oder gar
nicht (`C_LANEPLAYS:N` = 1 / 2 / 0). Lanes haben Namen (`P_LANENAME:n`, ab Werk 1, 2, 3 …,
Comp-Lanes C1, C2 …).

Zwei Arbeitsweisen, beide sind echt:

1. **Comping.** Mehrere Takes in Lanes, darüber eine Comp-Lane. Wer in einer Quell-Lane
   wischt, legt eine Comp-Area an; REAPER kopiert das Stück in die Comp-Lane. Die Quellen
   bleiben unberührt. Mehrere Comp-Lanes sind möglich, eine ist aktiv.
2. **Versionen / Playlists.** Jede Lane ist eine ganze Fassung der Spur (Gitarre A, B, C),
   genau eine spielt. Das ist Pro Tools' Playlist bzw. Cubase' Track Version. Früher per
   Skript (fricia „Playlist Folders“, Sexan „Virtual Tracks“), beide Autoren verweisen heute
   auf die nativen Lanes.

Was alle DAWs gemeinsam haben, die Verben des Comping:

| Verb | Pro Tools | Logic | Cubase | Studio One | REAPER nativ |
|---|---|---|---|---|---|
| Take hören / durchsteppen | Cycle Playlist | Next Take | Lane Solo | Pfeil ↑↓ | 42481/42482 Play only prev/next lane |
| Nur in der Auswahl steppen | Cycle Playlist within Selection | Quick Swipe | Comp Tool | Range | 42707/42708 Comp-Area up/down |
| In den Comp übernehmen | Copy Selection to Main | Swipe | Comp Tool | Range → Track | Wischen, 42475 Razor → Comp-Area |
| Ganze Version wechseln | Playlist wählen | Comp wählen | Track Version | Activate Layer | Play only lane N |
| A/B | – | – | – | – | 43701 Play only most recently playing lane |
| Take bewerten | Rating 1–5 | – | – | Umbenennen | 43155/43156 Rank (nur unter der Maus) |
| Phrase loopen | – | – | – | Shift-Klick | 42504 Loop to comp area (nur Maus) |

**An keiner Surface gibt es das.** EuCon hat „Next Playlist“ als Soft-Key-Befehl, Logic auf
Mackie Control kennt Takes gar nicht, der UF8 kann Comping nur über Tastenmakros. Ein Rad, mit
dem man bei laufender Schleife durch die Takes steppt und auf dem Display Name und Stand der
Lane sieht, existiert nirgends. Das ist die Lücke.

**Was REAPER-Nutzer im Forum stört:**
- „20 % meiner Zeit klappe ich Lanes auf und zu“ (Wunsch nach einer Lane-Übersicht).
- Nach einem Punch-In springt die spielende Lane auf den neuen Take.
- Bei Dutzenden Lanes den Überblick verlieren, welche spielen.
- Viele Comp-Actions gibt es nur „unter der Maus“ (Umbenennen, Loop auf Comp-Area, Rank).

---

## 2. Was die API hergibt, und was nicht

**Lesbar und schreibbar:** Lane-Modus (`I_FREEMODE`), Anzahl (`I_NUMFIXEDLANES`), Namen
(`P_LANENAME:n`, TCP frischt erst ab 7.70 sofort auf), wer spielt (`C_LANEPLAYS:N`,
`C_ALLLANESPLAY`), Lane eines Items (`I_FIXEDLANE`, danach `UpdateItemLanes`), ein-Lane-Ansicht
(`C_LANESCOLLAPSED`), Einstellungen (`C_LANESETTINGS`), Razor mit Lane-Höhe
(`P_RAZOREDITS_EXT`: `start end "" top_y bottom_y`, y als Bruchteil der Spurhöhe).

**Nur über Actions oder Chunk:**
- Comp-Areas: keine API. Anlegen über Razor + 42475, verschieben über 42707/42708 (für
  ausgewählte Items) oder die Maus-Actions.
- Welche Lane die aktive Comp-Lane ist und ob Comping an ist: nur Chunk `LANEREC rec comp
  lastcomp`.
- Masking, „Quelle bearbeiten beim Comping“: nur Chunk oder Toggle-Actions.

**Mindestversion:** REAPER 7.12 (ab da gibt es alles oben). Darunter bietet Rea-Sixty die
Lane-Funktionen nicht an.

---

## 3. Die Idee: vier Bausteine, ein Modell

Ein gemeinsamer Kern (reine Logik, testbar), darauf drei Oberflächen, die man einzeln
freigeben kann. Jede nutzt, was es schon gibt, und baut nichts nach.

### Baustein A: der Kern `LaneModel.h` (reine Logik, ctest)

- Nächste/vorige hörbare Lane (mit Wahl: Comp-Lanes überspringen oder nicht).
- „Nur diese Lane“ / „diese Lane dazu oder weg“ auf dem Spiel-Satz (exklusiv vs. Schichten).
- Razor-y für Lane i von n (Top/Bottom als Bruchteil).
- Gruppen: welche anderen Spuren denselben Schritt mitmachen (Media-Edit-Gruppe, alle 128
  Gruppen über den `trackGroups_`-Helfer vom VCA-Spill), mit derselben Lane-Nummer.
- A/B: die zuletzt spielende Lane merken (pro Spur).

Main-Thread-Teil in main.cpp: liest/schreibt die Attribute, Chunk-Leser für `LANEREC` mit
Cache (gleiche Machart wie `BUSCOMP` beim MCP-Collapse: nur bei Signaturwechsel neu lesen).

### Baustein B: UF1-Jog-Modus „Lanes“ (der Kern des Ganzen)

Siebter Jog-Modus, hinten angehängt (`Uf1JogMode::Lanes`, `kUf1JogModeCountForNav` 6 → 7,
fünf neue Nav-IDs, Bindings-Upgrade mit Werksbelegung). Erreichbar wie die anderen: SCRUB
halten + Jog, `jog_mode_lanes`, Werksbank „Jog Modes“.

**Das Rad:** steppt die hörende Lane der fokussierten Spur. Play läuft weiter, man hört den
nächsten Take ab der nächsten Detente. Das ist „Cycle Playlist“ am Rad.

**Das Kreuz** (pro Modus belegbar, Werksbelegung):

| Taste | Werk | Shift |
|---|---|---|
| ↑ / ↓ | Lane vor / zurück (wie das Rad, in Schritten) | Comp-Area unter dem Play-Cursor eine Lane hoch / runter |
| ← / → | Play-Cursor zum vorigen / nächsten Comp-Area-Rand bzw. Item-Rand | Loop auf die Comp-Area / das Item unter dem Cursor |
| Mitte | **Comp here**: die gehörte Lane übernimmt die Stelle (Zeitauswahl, sonst Razor, sonst das Item unter dem Cursor) | A/B: zurück zur vorigen Lane |

„Comp here“ ist das fehlende Hardware-Verb: hören, und wenn es sitzt, einen Knopf drücken.
Technisch: Razor mit Lane-y auf der gehörten Lane setzen, 42475. Die Mitte leuchtet, wenn
Comping auf der Spur an ist.

**Die Anzeige:** Kopfzeile wie bei den anderen Modi („JOG Lanes“), dazu Lane-Name und
„3/7“. Wo genau, klärt die Messung der Feldbreiten (8 Zeichen Trackname-Zone,
7-Segment-Zeitfeld kann K, M, V, W, X nicht). Vorschlag: der Name blitzt auf dem Trackname-
Feld beim Steppen, die Soft-Keys zeigen die Lanes (Baustein C).

**Der Fader** bleibt Spurlautstärke (linke Hälfte = Fader, Regel der Panelhälften). Optional
wie beim Items-Modus: „Fader = Take-Gain der gehörten Lane unter dem Cursor“, zum
Pegelangleichen von Takes beim Comping.

**LEDs:** ↑/↓ nur, wenn es oben/unten noch eine Lane gibt; Mitte = Comping an.

**Gruppen:** Ist die Spur in einer Media-Edit-Gruppe (Drums), steppen alle Gruppenspuren
mit Lanes dieselbe Lane-Nummer mit. Einstellung, Werk an.

### Baustein C: dynamische Bank „Lanes“ (UF8 und UF1)

Neue `DynamicBankKind::Lanes`, an den vier bekannten Stellen (Liste/UI, Resolver, Ausführer
UF8 + UF1, Anzahl/Blättern). Kontext = fokussierte Spur.

- **Label:** Lane-Name (`P_LANENAME`), gekürzt auf die Breite der Fläche; leer = Nummer.
- **Lampe nach der Side-Car-Regel:** hell = spielt, gedimmt = da, spielt nicht, dunkel =
  keine Lane. Aktive Comp-Lane in eigener Farbe.
- **Gesten** (die Bank bekommt Modifier wie die FX-Bank):
  - Druck = nur diese Lane spielen (Version wählen).
  - Shift = diese Lane zum Spiel-Satz dazu / weg (Schichten, wie Ctrl-Klick in REAPER).
  - Lang = diese Lane zur aktiven Comp-Lane machen (bzw. Comping mit ihr als Ziel).
  - Cmd = „Comp here“ aus dieser Lane (ohne vorher hinzusteppen).
- **Blättern:** UF1 wie immer ◄ ► lang; UF8 bankbar wie die FX-Bank.
  ⛔ Falle aus der Karte: `g_dynBankPage[8]` / `g_dynBankCtrl[8]` sind auf Art < 8 bemessen,
  Lanes wäre 12. Die Felder wachsen mit, sonst schreibt die Bank daneben.

Mit dem Jog-Modus zusammen ist das die Übersicht, die im Forum fehlt: vier Lanes mit Namen
auf dem UF1-Display, die spielende hell, das Rad steppt, die Lampe wandert mit.

### Baustein D: UF8 „Lanes“-Ansicht (Versionen auf acht Kanälen)

Acht Strips = acht Lanes der fokussierten Spur, gebaut nach dem Vorbild der Send-Ansicht
„This Track“ (`StripRoute`, die einzige Stelle, an der schon heute alle acht Strips
Unterobjekte einer Spur zeigen). Ein-/aus per Builtin, folgt dem Fokus, Bank ◄ ► blättert
bei mehr als acht Lanes.

| Element | Belegung |
|---|---|
| Scribble | Lane-Name (8 Zeichen) |
| Farbbalken | Spurfarbe, hell = spielt, dunkel = spielt nicht; aktive Comp-Lane markiert |
| SEL | nur diese Lane spielen |
| SOLO | diese Lane zum Spiel-Satz dazu / weg |
| CUT | Lane aus dem Spiel-Satz |
| Fader | Take-Gain des Items dieser Lane unter dem Play-Cursor (Pegel angleichen, im Ohr vergleichen) |
| V-Pot | dasselbe fein; Push = Loop auf dieses Item |
| Wertzeile | „Plays“ / „Comp“ / Item-Name unter dem Cursor |

Dazu passt der UF1 als Rad: UF8 zeigt alle Fassungen, UF1 steppt und übernimmt.

### Builtins (alle als Builtin, Kategorie „Lanes“, REAPER-Actions nur auf Wunsch)

`lane_next`, `lane_prev`, `lane_ab`, `lane_play_all`, `lane_play_comp` (nur aktive Comp-Lane),
`lane_comp_here`, `lane_comp_area_up`, `lane_comp_area_down`, `lane_loop_here`,
`lane_comping_toggle`, `lane_comp_new` (Comp in neue leere Lane), `lane_show_one_toggle`
(`C_LANESCOLLAPSED`), `lanes_fixed_toggle` (`I_FREEMODE`), `jog_mode_lanes`,
`uf8_lanes_view_toggle`. Alle auf die fokussierte Spur (plus Gruppe, wenn eingestellt).

### Banner, Einstellungen

- Banner: „Jog • Lanes“, beim UF8-Wechsel „UF8 • Lanes“. Beim Steppen kein Banner (zu
  laut), der UF1 zeigt es.
- Einstellungen (Behaviour → Lanes): „Lane steps skip comp lanes“ (Werk an), „Lanes follow
  the edit group“ (Werk an), „UF1 fader = take gain in Lanes mode“ (Werk aus).

---

## 4. Zuerst messen (Sonde, bevor irgendetwas gebaut wird)

Eine Lua-Sonde auf den Desktop, Log nach `/tmp`, Frank fährt sie an einem Comp-Projekt:

1. `LANEREC` im Chunk: welche Lane ist Comp-Lane, wie sieht „Comping aus“ aus.
2. `C_LANEPLAYS:N` während Comping an ist (spielt die Comp-Lane mit 1, die Quellen mit 0?).
3. Was 42481/42482 treffen (ausgewählte Spuren? überspringen sie Comp-Lanes?).
4. Was 42707/42708 und 41082/41083 treffen (ausgewähltes Item in der Comp-Lane? Cursor?).
5. `P_RAZOREDITS_EXT` y-Werte gegen `F_FREEMODE_Y/H` für Lane i von n, und ob 42475 danach
   genau diese Lane als Quelle nimmt.
6. Takes in Items in Lanes: gibt es ein Rank-Attribut per API, oder nur die Maus-Actions?

Danach steht fest, was über die API geht und was über Actions mit vorher gesetzter Auswahl.

---

## 5. Reihenfolge

1. Sonde (Abschnitt 4).
2. Kern `LaneModel.h` + ctest.
3. Baustein B, UF1-Jog-Modus „Lanes“, samt Builtins, Nav-Werksbelegung, Banner, Handbuch.
4. Baustein C, dynamische Bank „Lanes“.
5. Baustein D, UF8-Ansicht „Lanes“.

Jeder Schritt ist für sich nutzbar. B allein ist schon das, was es nirgends gibt.

---

## 6. Entscheide für Frank

1. Reihenfolge B → C → D, oder die UF8-Ansicht (D) zuerst?
2. UF8-Fader in der Lanes-Ansicht: Take-Gain unter dem Cursor, oder die Spur bleibt auf dem
   Fader?
3. Gruppen: Lanes folgen der Media-Edit-Gruppe, Werk an?
4. Rad in der Grundstellung: ganze Spur steppen (Versionen/Audition) und Shift = Comp-Area,
   oder umgekehrt?
5. „Comp here“ auf die Mitte des Kreuzes, oder die Mitte bleibt `jog_content_drag` wie in den
   anderen Modi?

---

## Quellen

- SDK-Header `extension/vendor/reaper-sdk/sdk/reaper_plugin_functions.h` (7.67),
  ReaScript-Doku https://www.reaper.fm/sdk/reascript/reascripthelp.html (7.81)
- REAPER User Guide 7.81, Kap. 8.12 https://www.reaper.fm/userguide/ReaperUserGuide781c.pdf
- Changelog https://www.reaper.fm/whatsnew.txt
- Chunk-Doku https://github.com/ReaTeam/Doc/blob/master/State%20Chunk%20Definitions
- Action-Liste https://www.extremraym.com/cloud/reaper-action-list/
- Pro Tools Playlists https://www.soundonsound.com/techniques/pro-tools-how-work-playlists,
  https://www.production-expert.com/home-page/2018/10/5/audition-pro-tools-playlists-on-the-fly-do-you-know-how-to-use-these-two-shortcuts
- Avid EUCON Commands https://resources.avid.com/SupportFiles/ProMixing/Pro_Tools_EUCON_Commands_v2025.12.pdf
- Logic Control Surfaces Guide https://help.apple.com/pdf/logicpro-css/en_US/logic-pro-control-surfaces-support-guide.pdf
- Cubase Track Versions https://archive.steinberg.help/nuendo/v11/en/cubase_nuendo/topics/track_handling/trackhandling_activating_trackversions_on_multiple_tracks_t.html
- Studio One Comping https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Comping.htm
- Ableton Comping https://www.ableton.com/en/manual/comping
- Forum: https://forums.cockos.com/showthread.php?t=291607, https://forum.cockos.com/showthread.php?p=2846050,
  https://forum.cockos.com/showthread.php?t=283665, https://forums.cockos.com/showthread.php?t=290705,
  https://forum.cockos.com/showthread.php?p=2900845
