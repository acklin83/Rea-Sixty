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
| ← / → | Play-Cursor zum vorigen / nächsten Comp-Area-Rand bzw. Item-Rand | ← Loop auf die Comp-Area / das Item unter dem Cursor, → A/B (der ganze vorige Spielsatz) |
| Rad | gehörte Lane steppen | Comp-Area unter dem Cursor steppen (wie Shift ↑ / ↓) |
| Mitte tippen | **Comp here** | gehörte Lane in den Spielsatz / heraus |
| Mitte halten + Rad | **malen** (Baustein F) | |
| Mitte lang, ohne Rad | Live-Comping an / aus (Baustein F) | |

Ob die Mitte beim Halten malt oder beim Loslassen Live-Comping schaltet, entscheidet das Rad:
wurde während des Haltens gedreht, war es ein Strich, und das Loslassen schaltet nichts.

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
- **Blättern:** UF1 wie immer ◄ ► lang; UF8 bankbar wie die FX-Bank. Steppt das Rad auf eine
  Lane ausserhalb der sichtbaren Seite, blättert die Bank von selbst mit.
- **Spur ohne Lanes:** alle Tasten dunkel, bis auf eine: „Lanes on“ (`lanes_fixed_toggle`).
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

### Baustein E: der Spielsatz (eine, mehrere, alle, keine Lane)

REAPER lässt jede Lane einzeln spielen (`C_LANEPLAYS:N`: 1 = exklusiv, 2 = mit anderen,
0 = stumm), dazu `C_ALLLANESPLAY` (0 keine, 1 alle, 2 einige). Mehrere Lanes zugleich braucht
man für Dopplungen, mehrere Mikrofone eines Takes, Schichten beim Sounddesign, und zum
Gegenhören „alles“. Der Plan behandelt den **Spielsatz** als eigenes Ding, nicht als
Sonderfall:

- **Anzeige überall gleich:** jede spielende Lane hell, jede stumme gedimmt, auf UF8-Bank,
  UF1-Soft-Keys und UF8-Ansicht. Auf dem UF1 zusätzlich „1/7“, „3 von 7“ oder „alle 7“.
- **Einzelne Lane rein/raus:** Shift auf der Bank-Taste, SOLO in der UF8-Ansicht, Shift+Mitte
  im Jog-Modus. Aus exklusiv wird damit ein Satz; nimmt man bis auf eine alle heraus, ist es
  wieder exklusiv.
- **Alle / keine / nur Comp:** eigene Builtins (`lane_play_all`, `lane_play_none`,
  `lane_play_comp`). Werksbelegung in einer neuen UF1-Werksbank „Lanes“ neben „Jog Modes“,
  nicht auf ◄ / ►: die blättern Bänke, und die Beschriftung einer Taste hat Vorrang.
- **Das Rad bei mehreren spielenden Lanes:** es wählt weiter EINE Lane zum Vorhören (exklusiv),
  merkt sich aber den ganzen Satz davor. A/B holt den ganzen Satz zurück, nicht nur eine Lane.
  So bleibt „kurz reinhören, zurück zur Schichtung“ ein Handgriff.
- **Gruppen:** der Satz wird als Muster übertragen (Lanes 1, 3 und 4 spielen → auf jeder
  Gruppenspur spielen 1, 3 und 4), soweit die Spur so viele Lanes hat.
- **Aufnahme:** nach einem Punch-In schaltet REAPER auf die neue Lane (Forumsklage). Rea-Sixty
  greift da nicht ein, zeigt es aber sofort an (Lampe springt), und A/B bringt den Satz davor
  zurück.

### Baustein F: Malen (Comp-Areas von der Surface)

Das Wischen mit der Maus ist in REAPER der Kern des Comping: man zieht über die Stelle einer
Quell-Lane, und die Comp-Lane übernimmt sie. Drei Arten, das an der Hardware zu tun, von
einfach bis spektakulär:

1. **Mit dem Rad malen (UF1).** Mitte des Kreuzes halten und das Rad drehen: der Cursor fährt,
   und die überfahrene Strecke wird aus der gehörten Lane in die Comp-Lane gemalt. Loslassen
   beendet den Strich. Rückwärts drehen nimmt zurück, was im selben Strich zu weit ging. Das
   ist Wischen mit dem Rad statt mit der Maus.
   - **Mithören ist eine Einstellung, ab Werk aus** („Scrub while painting“). Viele wollen das
     Gegarble eines Scrubs nicht hören; ohne fährt der Cursor stumm, und die gemalte Strecke
     ist im Arrange und auf dem UF1-Display zu sehen.
   - Stattdessen ab Werk an: **„Play the stroke after painting“**. Nach dem Loslassen spielt
     REAPER den Strich einmal ab, mit kurzem Vorlauf (Einstellung, Werk 1 s), und stoppt
     danach wieder dort, wo er vorher stand. So hört man den Übergang sauber statt verzerrt.
   - Beide aus: stumm malen, selber abspielen.
   - Während des Strichs zeigt ein Razor auf der Höhe der Lane die Strecke im Arrange; der
     UF1 zeigt „Paint“, die Lane und die Länge.
2. **Live-Comping beim Abspielen („Schnittpult“).** Malen einschalten (Builtin
   `lane_paint_live`, lang auf der Mitte), Play drücken, und bei jedem Lane-Wechsel mit dem Rad,
   dem Kreuz, der Bank oder SEL schreibt Rea-Sixty ab dieser Stelle die neue Lane in den Comp.
   Man spielt den Song durch und schneidet wie ein Bildmischer: Strophe Take 2, Refrain Take 5,
   letzte Zeile Take 1. Mit laufender Schleife überschreibt jeder Durchgang die Stelle, bis sie
   sitzt. Stop schliesst den letzten Abschnitt ab.
3. **Acht Lanes als Schnitttasten (UF8).** In der UF8-Ansicht mit Malen an ist jedes SEL eine
   Schnitttaste: drücken, und ab jetzt kommt diese Lane in den Comp. Acht Takes nebeneinander,
   Namen auf den Scribbles, die spielende hell. Das kann keine andere Surface.

Gemeinsame Regeln fürs Malen:
- Schnittpunkte rasten optional am Raster (Einstellung „Snap paint to grid“, Werk aus), sonst
  am Play-Cursor. Ein kleiner Vorlauf (Einstellung, z. B. 20 ms) setzt den Schnitt vor die
  Detente, damit Konsonanten nicht abgeschnitten werden.
- Überblendungen macht REAPER selbst (Option „Auto-crossfade when comping“, 42631); Rea-Sixty
  schaltet sie nicht um.
- Ein Strich bzw. ein Live-Durchgang = ein Undo-Schritt.
- Technik: jeder Abschnitt wird als Razor mit der Lane-Höhe der Quell-Lane gesetzt und über
  42475 in eine Comp-Area verwandelt; ist Comping auf der Spur aus, schaltet der erste Strich
  es ein (REAPER legt dann C1 an). Ob das während der Wiedergabe sauber geht, klärt die Sonde.
- Gruppen: gemalt wird auf allen Gruppenspuren derselben Lane-Nummer (Drums in einem Zug).

**Takes in Items** (das alte REAPER-Takesystem, mehrere Takes in einem Item): Der Lanes-Modus
kann sie mitbedienen. Steht unter dem Cursor ein Item mit mehreren Takes und die Spur hat keine
Lanes, steppt das Rad den aktiven Take (`I_CURTAKE`), der Name steht wie der Lane-Name da, und
„Comp here“ bzw. Malen teilt das Item an den Schnittpunkten und setzt dort den gewählten Take
aktiv. So funktioniert das Comping auch für alle, die noch mit Takes statt Lanes arbeiten.

### Baustein G: UF8-Encoder-Modus „Lanes“ mit eigenem Kreuz

**Was es heute gibt:** der UF8 hat 15 Encoder-Modi (`EncoderMode`, main.cpp:6481: Channel
Select, Nudge, Markers, FX-Cycle, …), gewählt über NUDGE / FOCUS und die `encoder_*`-Builtins.
Das Kreuz daneben (Zoom-Pad) ist fest Zoom (`zoom_up` … `zoom_center` = FIT, Bindings.cpp:1095),
egal in welchem Modus. Der UF1 hat, was hier fehlt: pro Jog-Modus eigene Kreuz-Belegungen
(`perModeNavId`, `uf1RemapNavForJogMode_`), mit Rückfall auf die Grundbelegung, wo nichts
eigenes gesetzt ist (`fillDerivedUf1Slots_`).

**Vorschlag, bei dieser ersten Gelegenheit das Muster für den UF8 anzulegen:**

1. **`EncoderMode::Lanes`**, hinten angehängt, Builtin `encoder_lanes`. Auf eine freie Taste
   oder eine Quick-Belegung; die Encoder-Modus-Tasten behalten ihre Beschriftung.
2. **Das Kreuz folgt dem Encoder-Modus**, nach demselben Muster wie beim UF1: eine
   Umleitung `uf8RemapCrossForEncMode_` an der einen Stelle, an der das Zoom-Pad in die
   Bindings geht, und eigene Belegungs-IDs pro Modus. Aber nur für Modi, die ein eigenes Kreuz
   mitbringen, vorerst nur Lanes (5 IDs, mit ENC PUSH 6). Alle anderen Modi fallen auf die
   Zoom-Grundbelegung zurück, für sie ändert sich nichts. Weitere Modi können später je fünf
   IDs dazubekommen.
3. **Dieselbe Tabelle wie am UF1**, dieselben Builtins, derselbe Kern: Encoder = Lane steppen,
   Shift + Encoder = Comp-Area steppen, ↑ / ↓ Lane, ← / → Ränder, Shift + ← Loop,
   Shift + → A/B, Mitte (FIT) tippen = Comp here, Mitte halten + Encoder = malen, Mitte lang =
   Live-Comping. Wer beide Flächen hat, hat an beiden dieselben Griffe.
4. **Anzeige ohne grosses Display:** die Top-Soft-Keys zeigen die Bank „Lanes“ (acht Namen,
   Lampen = Spielsatz). Beim Steppen zeigt die Wertzeile des fokussierten Strips „Lane“ und
   den Namen der gehörten Lane (je 8 Zeichen), beim Malen „Paint“. Ob der Lanes-Modus die
   Bank „Lanes“ selbst auf die Top-Keys holt, ist ein Entscheid (unten).
5. **Encoder-Auflösung:** der Kanal-Encoder schickt mehrere Ereignisse pro Rastung; Lanes
   benutzt denselben Sammler wie Channel Select (`kChannelEncoderScale`), eine Rastung = eine
   Lane.
6. **UF8 allein reicht:** mit G und D (Lanes-Ansicht) ist der ganze Comping-Ablauf ohne UF1
   machbar. Mit UF1 teilen sich beide denselben Zustand, weil der Spielsatz in REAPER selbst
   steht (`C_LANEPLAYS`) und A/B pro Spur gemerkt wird.

**Nur genannt, nicht Teil dieses Plans:** Mit dem Kreuz-pro-Modus-Muster könnten später auch
die UF1-Jog-Modi (Playhead, Scrub, Items, Envelope, Razor, Fades) als UF8-Encoder-Modi
kommen. Dann kann ein UF8 ohne UF1 editieren.

### Builtins (alle als Builtin, Kategorie „Lanes“, REAPER-Actions nur auf Wunsch)

`lane_next`, `lane_prev`, `lane_ab`, `lane_play_all`, `lane_play_none`, `lane_play_comp`
(nur aktive Comp-Lane), `lane_play_toggle` (gehörte Lane in den Satz / heraus),
`lane_paint_live` (Live-Comping an/aus), `lane_paint_hold` (Malen, solange gehalten),
`lane_paint_discard` (laufenden Live-Durchgang verwerfen, nur bei Entscheid 6 = „bei Stop“),
`lane_comp_here`, `lane_comp_area_up`, `lane_comp_area_down`, `lane_loop_here`,
`lane_comping_toggle`, `lane_comp_new` (Comp in neue leere Lane), `lane_show_one_toggle`
(`C_LANESCOLLAPSED`), `lanes_fixed_toggle` (`I_FREEMODE`), `jog_mode_lanes`,
`uf8_lanes_view_toggle`, `encoder_lanes`. Alle auf die fokussierte Spur (plus Gruppe, wenn
eingestellt).

### Banner, Einstellungen

- Banner: „Jog • Lanes“, beim UF8-Wechsel „UF8 • Lanes“, Live-Comping „Lanes • Live paint“
  an / aus. Beim Steppen kein Banner (zu laut), der UF1 zeigt es.
- Einstellungen (Behaviour → Lanes): „Lane steps skip comp lanes“ (Werk an), „Lanes follow
  the edit group“ (Werk an), „UF1 fader = take gain in Lanes mode“ (Werk aus), „Scrub while
  painting“ (Werk aus), „Play the stroke after painting“ (Werk an, Vorlauf 1 s), „Snap paint
  to grid“ (Werk aus), „Paint lead-in“ (Werk 20 ms).

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
7. Razor + 42475 während der Wiedergabe: entsteht die Comp-Area sauber, ohne Aussetzer?
8. Comping bei geschichteten Lanes (mehrere spielen): was spielt, was wird in den Comp kopiert?
9. Takes: `I_CURTAKE` setzen und Item an Schnittpunkten teilen, ein Undo-Schritt?

Danach steht fest, was über die API geht und was über Actions mit vorher gesetzter Auswahl.

---

## 5. Reihenfolge

1. Sonde (Abschnitt 4).
2. Kern `LaneModel.h` + ctest.
3. Baustein B + E + G, der Lanes-Modus mit Spielsatz auf UF1 (Rad) und UF8 (Encoder, Kreuz
   pro Modus) zusammen, weil beide dieselben Builtins und dieselbe Tabelle benutzen. Samt
   Nav-Werksbelegung, Banner, Handbuch.
4. Baustein F.1 Malen mit dem Rad, dann F.2 Live-Comping.
5. Baustein C, dynamische Bank „Lanes“.
6. Baustein D, UF8-Ansicht „Lanes“, mit F.3 (SEL als Schnitttasten).
7. Takes in Items im Lanes-Modus.

Jeder Schritt ist für sich nutzbar. B allein ist schon das, was es nirgends gibt.

---

## 6. Entscheide für Frank

1. Reihenfolge B → C → D, oder die UF8-Ansicht (D) zuerst?
2. UF8-Fader in der Lanes-Ansicht: Take-Gain unter dem Cursor, oder die Spur bleibt auf dem
   Fader?
3. Gruppen: Lanes folgen der Media-Edit-Gruppe, Werk an?
4. Rad in der Grundstellung: ganze Spur steppen (Versionen/Audition) und Shift = Comp-Area,
   oder umgekehrt?
5. „Comp here“ / Malen auf die Mitte des Kreuzes, oder die Mitte bleibt `jog_content_drag` wie
   in den anderen Modi?
6. Live-Comping: schreibt jeder Lane-Wechsel beim Abspielen sofort, oder erst nach Stop (dann
   ist ein verpatzter Durchgang mit einem Druck verworfen)?
7. Takes in Items mit in den Plan, oder nur Lanes?
8. UF8 im Lanes-Encoder-Modus: wird ENC PUSH auch pro Modus belegbar (Werk: Comp here), oder
   bleibt er „Plug-in-Fenster“?
9. Holt der Lanes-Encoder-Modus die Bank „Lanes“ selbst auf die UF8-Top-Keys (und beim
   Verlassen die vorige zurück), oder wählt man die Bank selbst?

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

---

## 7. Fallstudien

Jede Fallstudie geht einen echten Arbeitsgang durch: was man in der Hand hat, was man drückt,
was Rea-Sixty dabei in REAPER tut, und was man sieht und hört. Die Bedienung ist die aus dem
Plan mit den Werksbelegungen; wo ein offener Entscheid hineinspielt, steht es dabei.

### Fall 1: Lead-Vocal aus sechs Takes, Zeile für Zeile

**Ausgangslage:** Spur „Lead Vox“, Fixed Lanes an, sechs Takes in Lanes 1 bis 6, noch kein
Comp. Die Sängerin ist weg, jetzt wird ausgesucht.

1. **SEL auf „Lead Vox“** (UF8). Die Spur ist fokussiert; UF1 und die Lanes-Bank folgen ihr.
2. **SCRUB halten, Rad auf „Lanes“.** Kopfzeile auf dem UF1: „JOG Lanes“. Die Soft-Keys zeigen
   (mit Bank „Lanes“) die Lanes 1 bis 4 mit Namen; Lane 1 hell, weil sie spielt.
3. **Cursor an die erste Zeile, Shift + ←** = Loop auf das Item unter dem Cursor (noch keine
   Comp-Area da). Rea-Sixty setzt die Loop-Punkte auf die Item-Grenzen von Lane 1 und schaltet
   Repeat ein. **Play.**
4. **Rad drehen.** Jede Detente: nur die nächste Lane spielt (`C_LANEPLAYS`, exklusiv). Man
   hört in der Schleife Take 2, 3, 4 … Die Lampe wandert mit, der UF1 zeigt den Lane-Namen und
   „4/6“. Comp-Lanes gibt es noch keine, also nichts zu überspringen.
5. **Take 4 sitzt: Mitte tippen („Comp here“).** Rea-Sixty setzt einen Razor über die Loop-
   Strecke auf die Höhe von Lane 4 und löst 42475 aus. Comping war aus, also legt REAPER die
   Comp-Lane C1 oben an, und die Zeile aus Take 4 steht darin. Die Mitte leuchtet (Comping an).
   Was danach spielt (C1 oder weiter die gehörte Lane), ist REAPERs Verhalten beim Comping;
   das klärt Sonde Frage 2, und die Lampen zeigen es.
6. **→** springt zum nächsten Item-Rand, also zur nächsten Zeile. **Shift + ←** setzt die
   Schleife dorthin. Weiter bei Schritt 4.
7. **Eine Silbe aus Take 2 retten: Mitte halten und Rad drehen** über genau die Silbe (Malen,
   Fall 3 im Detail). Loslassen spielt den Strich ab (Werkseinstellung).
8. **Gegenprobe: Shift + →** (A/B) springt zwischen dem Comp und dem zuletzt gehörten Take.

**Ergebnis:** C1 enthält den Comp, die sechs Takes sind unangetastet. Jeder „Comp here“ ist ein
eigener Undo-Schritt.

### Fall 2: Eine Stelle nachträglich tauschen

**Ausgangslage:** Comp C1 steht, in Takt 23 klingt das „s“ aus Take 3 zu scharf.

1. Cursor in die Comp-Area in Takt 23 (Rad im Playhead-Modus oder ← / → im Lanes-Modus, die
   an Comp-Area-Rändern halten).
2. **Shift + ↑ / ↓** (oder Shift + Rad): die Comp-Area unter dem Cursor wandert eine Lane hoch
   oder runter. Technisch: das Comp-Lane-Item unter dem Cursor auswählen, 42707 / 42708.
   Hörbar sofort, wenn die Schleife läuft; die Anzeige nennt die Quell-Lane.
3. Passt es nicht an der Grenze: ← / → an den Rand, dann Mitte halten + Rad, um die Grenze
   nachzumalen.

### Fall 3: Malen mit dem Rad, stumm und mit Vorhören

**Ausgangslage:** Wie Fall 1, gehörte Lane ist Take 5. „Scrub while painting“ ist aus (Werk),
„Play the stroke after painting“ an (Werk).

1. Cursor kurz vor die Stelle.
2. **Mitte halten, Rad nach rechts.** Der Cursor fährt stumm Detente um Detente (Schrittweite
   wie im Playhead-Modus: Raster, Takte oder Sekunden). Im Arrange wächst eine Markierung auf
   der Höhe von Lane 5; der UF1 zeigt „Paint 5“ und die Länge.
3. Zu weit gefahren: **Rad zurück**, die Markierung schrumpft mit.
4. **Loslassen.** Rea-Sixty setzt den Razor über die Strecke auf Lane 5, 20 ms Vorlauf
   (Einstellung), 42475. Ein Undo-Schritt. Dann spielt REAPER ab 1 s vor dem Strich bis zu
   seinem Ende und stoppt wieder, der Cursor steht wieder da, wo er vorher war.
5. Wer lieber mithört: „Scrub while painting“ an, dann klingt Schritt 2 wie ein Scrub.

### Fall 4: Live-Comping eines Gitarrensolos (Schnittpult)

**Ausgangslage:** Spur „Solo Gtr“, fünf Takes in Lanes 1 bis 5. UF8 in der Lanes-Ansicht,
acht Strips, davon fünf belegt, Namen auf den Scribbles.

1. **Mitte lang** (UF1) oder das Builtin `lane_paint_live` auf einem Soft-Key: Live-Comping an.
   Banner „Lanes • Live paint“, die Mitte leuchtet.
2. **Play** vor dem Solo.
3. **SEL auf Strip 2** in Takt 1 des Solos, **SEL auf Strip 5** in Takt 5, **SEL auf Strip 1**
   für die letzte Phrase. Jede SEL: Rea-Sixty merkt Zeitpunkt und Lane; ab da hört man diese
   Lane (exklusiv). Der Farbbalken der gewählten Lane leuchtet hell.
4. **Stop.** Die Abschnitte [Start → Takt 5: Lane 2], [Takt 5 → letzte Phrase: Lane 5],
   [letzte Phrase → Stop: Lane 1] werden als Comp-Areas in C1 geschrieben, ein Undo-Schritt
   für den ganzen Durchgang.
   - Entscheid 6: entweder schreibt jeder SEL sofort (man sieht die Areas entstehen), oder
     erst bei Stop (ein verpatzter Durchgang lässt sich mit einem Druck verwerfen, statt Undo).
5. **Mit Schleife:** Loop über das Solo, Live-Comping an. Jeder Durchgang schreibt seine
   Wechsel über den vorigen; wenn es sitzt, Live-Comping aus.

Ohne UF8 geht dasselbe mit dem Rad oder ↑ / ↓ auf dem UF1: jeder Lane-Schritt ist ein Schnitt.

### Fall 5: Drums in der Gruppe comppen

**Ausgangslage:** Kick, Snare, OH L/R, Room, alle in Media-Edit-Gruppe 3, jede mit vier Takes
in Lanes 1 bis 4. „Lanes follow the edit group“ an (Werk).

1. **SEL auf Snare**, Jog-Modus Lanes.
2. **Rad:** jeder Schritt schaltet auf allen fünf Spuren dieselbe Lane (Gruppe über alle 128
   Gruppen, `trackGroups_`). Man hört das ganze Set aus Take 3.
3. **Fill in Takt 16 aus Take 3:** Zeitauswahl über den Fill (am Rechner oder mit dem
   Razor-Modus), **Mitte tippen**. Rea-Sixty setzt den Razor auf Lane 3 aller Gruppenspuren,
   42475 legt auf allen fünf die Comp-Area an. Phasengleich, weil alle Spuren dieselbe Strecke
   aus derselben Lane bekommen.
4. **Eine Spur allein** (Room nur aus Take 1): Gruppe kurz aus (REAPER-Gruppen-Schalter) oder
   die Einstellung aus, dann nur Room fokussieren.

### Fall 6: Drei Bass-Versionen (Playlist-Betrieb)

**Ausgangslage:** Spur „Bass“, Lane 1 „Finger“, Lane 2 „Pick“, Lane 3 „Synth“, jede eine ganze
Fassung, exklusiv. Kein Comping.

1. **UF8 Lanes-Ansicht** (Soft-Key `uf8_lanes_view_toggle`). Strips 1 bis 3 heissen Finger,
   Pick, Synth; Finger hell.
2. **Play, SEL auf „Pick“.** Nur Lane 2 spielt, im Takt, ohne Aussetzer. SEL „Synth“,
   SEL „Finger“: Versionen im Kontext des Songs vergleichen.
3. **Fader auf „Synth“ ziehen** (Entscheid 2, Fader = Take-Gain unter dem Cursor): die Synth-
   Fassung ist 3 dB lauter, runter, damit der Vergleich fair ist. Das ändert das Item in Lane 3
   unter dem Cursor, nicht die Spur.
4. **A/B** (Shift + → auf dem UF1): zwischen den zwei zuletzt gehörten Fassungen hin und her.
5. Entschieden: „Pick“ bleibt die spielende Lane und wird so gespeichert.

### Fall 7: Gedoppelte Gitarre, mehrere Lanes zugleich

**Ausgangslage:** Spur „Rhythm Gtr“, Lanes 1 bis 4 sind vier Einspielungen. Gewollt: 1 und 3
zusammen als Dopplung.

1. **Bank „Lanes“** (UF1-Soft-Keys oder UF8-Top-Keys): Druck auf Lane 1 = nur Lane 1.
   **Shift + Druck auf Lane 3** = Lane 3 dazu. Beide Tasten hell, der UF1 zeigt „2 von 4“.
2. Dasselbe in der UF8-Ansicht: SEL auf 1, SOLO auf 3.
3. **Kurz Lane 4 allein hören:** Rad eine Detente, nur Lane 4 spielt (Vorhören ist exklusiv).
4. **Zurück zur Dopplung: Shift + →** (A/B) holt den ganzen Satz 1 + 3 zurück, nicht nur eine
   Lane.
5. **Alles gegenhören:** `lane_play_all` aus der Werksbank „Lanes“, dann wieder A/B.

### Fall 8: Nachaufnahme im Punch-In

**Ausgangslage:** Comp C1 spielt. Für Takt 40 wird ein neuer Take aufgenommen; REAPER legt ihn
in eine neue Lane und lässt diese spielen (die Forumsklage).

1. Aufnahme wie gewohnt (Transport, UF1 REC).
2. Nach dem Stop springt die helle Lampe sichtbar auf die neue Lane 7: man sieht sofort, dass
   jetzt der Rohtake spielt statt des Comps.
3. **Shift + →** (A/B): zurück auf C1, so wie es vorher war.
4. Takt 40 aus Lane 7 übernehmen: Cursor dorthin, Rad auf Lane 7 (vorhören), **Mitte**.

Rea-Sixty ändert REAPERs Aufnahmeverhalten nicht; es macht es sichtbar und in einem Griff
rückgängig.

### Fall 9: Takes in einem Item (ohne Lanes)

**Ausgangslage:** Spur „Keys“ ohne Fixed Lanes, ein Item mit fünf Takes (REAPERs Takesystem).
Entscheid 7 vorausgesetzt.

1. Jog-Modus Lanes, Cursor im Item. Die Anzeige sagt „Takes“ statt „Lanes“ und nennt den
   aktiven Take.
2. **Rad:** der aktive Take des Items unter dem Cursor steppt (`I_CURTAKE`), man hört ihn.
3. **Mitte (Comp here)** mit Zeitauswahl: das Item wird an den Grenzen geteilt, im Mittelteil
   ist der gewählte Take aktiv. Malen mit dem Rad genauso, an den Strichgrenzen geteilt.
4. Wer lieber auf Lanes arbeitet: `lanes_fixed_toggle` bzw. REAPERs „convert takes to lanes“
   (42661), dann gilt alles aus Fall 1.

### Fall 10: Viele Lanes

**Ausgangslage:** 14 Lanes auf einer Spur.

- UF1-Soft-Keys: vier sichtbar, **◄ / ► lang** blättert innerhalb der Bank (wie bei jeder
  dynamischen Bank), die Seite steht auf dem Display.
- UF8-Top-Keys: acht sichtbar, blättern wie bei der FX-Bank.
- UF8-Ansicht: Strips 1 bis 8, **Bank ◄ / ►** zeigt 9 bis 14.
- Das Rad kennt keine Seiten: es steppt durch alle 14, und die Bank blättert von selbst mit, damit
  die gehörte Lane immer auf einer Taste zu sehen ist.

### Fall 11: Spur ohne Lanes

**Ausgangslage:** Lanes-Modus an, fokussiert ist eine normale Spur mit einem Take.

- UF1: „No lanes“ in der Kopfzeile, Rad und Kreuz tun nichts, kein Fehler.
- Bank „Lanes“: alle Tasten dunkel, bis auf eine: „Lanes on“ (`lanes_fixed_toggle`).
- UF8-Ansicht: Strips leer, SEL tut nichts.
- Sobald man eine Spur mit Lanes fokussiert, ist alles wieder da. Kein Modus springt von selbst
  um.

### Fall 12: Einen Durchgang verwerfen

- **Comp here / Malen:** jeder Strich ist ein Undo-Schritt, REAPERs Undo nimmt ihn zurück.
- **Live-Comping:** der ganze Durchgang ist ein Undo-Schritt. Mit Entscheid 6 = „erst bei
  Stop“ kommt dazu: `lane_paint_discard` verwirft den laufenden Durchgang, bevor etwas
  geschrieben ist.
- **Spielsatz:** A/B ist das Undo für „wer spielt“; REAPER legt dafür keinen Undo-Schritt an.

### Fall 13: Comping nur mit dem UF8

**Ausgangslage:** kein UF1. Spur „Lead Vox“ wie in Fall 1, UF8 Top-Keys auf Bank „Lanes“.

1. **SEL auf „Lead Vox“**, dann **`encoder_lanes`** (Taste nach Wahl). Banner „Encoder •
   Lanes“; das Zoom-Pad ist jetzt das Lanes-Kreuz.
2. **Shift + ←** (Zoom-Pad) = Loop auf die Zeile, **Play**.
3. **Kanal-Encoder drehen:** Take für Take; die Top-Key-Lampe wandert mit, die Wertzeile des
   Vox-Strips zeigt „Lane“ und den Namen.
4. **FIT (Mitte) tippen:** Comp here, wie Fall 1 Schritt 5.
5. **FIT halten und Encoder drehen:** malen, wie Fall 3.
6. **Zurück zum Mischen:** `encoder_nudge` oder die Modus-Taste, die man gewohnt ist. Das
   Zoom-Pad zoomt wieder, die Top-Keys sind wieder auf ihrer Bank (je nach Entscheid 9).

