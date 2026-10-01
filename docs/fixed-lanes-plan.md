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
- Comp-Areas: keine API, aber **lesbar im Spur-Chunk** (gemessen 01.10.): eine Zeile pro
  Area, `LINKEDLANE start ende quell-lane 0 -1 blende-ein blende-aus` (Quell-Lane 0-basiert,
  Blenden 0.01). Anlegen über Razor + 42475, verschieben über 42707/42708 (für das
  ausgewählte Comp-Lane-Item) oder die Maus-Actions.
- Welche Lane die aktive Comp-Lane ist und ob Comping an ist: nur Chunk `LANEREC`, Feld 2 =
  Index der Comp-Lane, -1 = Comping aus (gemessen; Feld 4 ändert sich mit, Bedeutung offen).
  Die Comp-Lane heisst nicht zwingend „C1“: in der Sonde hiess sie weiter „1“. Erkennen also
  immer über `LANEREC`, nie über den Namen.
- Masking, „Quelle bearbeiten beim Comping“: nur Chunk oder Toggle-Actions.

**Mindestversion:** REAPER 7.12 (ab da gibt es alles oben). Darunter bietet Rea-Sixty die
Lane-Funktionen nicht an.

---

## 3. Die Idee: ein Kern, ein Modus pro Fläche

Ein gemeinsamer Kern (reine Logik, testbar), darauf der Modus „Lanes“ auf beiden Flächen:
am UF1 als Jog-Modus, am UF8 als Encoder-Modus, beide mit eigenem Kreuz und denselben
Builtins. Was der Modus mitnimmt, ist wählbar (die Bank „Lanes“ auf den Soft-Keys, die acht
Strips als Option). Jeder Baustein nutzt, was es schon gibt, und baut nichts nach.

| Baustein | Was |
|---|---|
| A | Kern `LaneModel.h` |
| B | UF1-Jog-Modus Lanes |
| C | dynamische Bank „Lanes“ (UF8 und UF1) |
| D | Option „Lanes on the strips“ des UF8-Encoder-Modus |
| E | Spielsatz (eine, mehrere, alle, keine Lane) |
| F | Malen (Rad, Live-Comping, SEL als Schnitttasten) |
| G | UF8-Encoder-Modus Lanes mit Kreuz pro Encoder-Modus |
| H | Modus-Wahl am UF8: Dropdown im Editor, Karussell am Gerät |

### Baustein A: der Kern `LaneModel.h` (reine Logik, ctest)

- Nächste/vorige hörbare Lane (mit Wahl: Comp-Lanes überspringen oder nicht).
- „Nur diese Lane“ / „diese Lane dazu oder weg“ auf dem Spiel-Satz (exklusiv vs. Schichten).
- Razor-y für Lane i von n (Top/Bottom als Bruchteil).
- Gruppen: welche anderen Spuren denselben Schritt mitmachen (Media-Edit-Gruppe, alle 128
  Gruppen über den `trackGroups_`-Helfer vom VCA-Spill), mit derselben Lane-Nummer.
- A/B: die zuletzt spielende Lane merken (pro Spur).

Main-Thread-Teil in main.cpp: liest/schreibt die Attribute, Chunk-Leser für `LANEREC` und
`LINKEDLANE` mit Cache (gleiche Machart wie `BUSCOMP` beim MCP-Collapse: nur bei
Signaturwechsel neu lesen). Aus `LINKEDLANE` kommt die **Comp-Karte**: welche Stelle aus
welcher Lane stammt. Der UF1 kann sie zeigen, ← / → springen an ihre Grenzen, und „Comp-Area
unter dem Cursor“ weiss, aus welcher Lane sie kommt.

**Kein Undo-Schritt beim Steppen:** REAPERs eigene Lane-Actions legen pro Wechsel einen
Eintrag „Change lane play state“ an (gemessen, acht Einträge für acht Klicks). Rea-Sixty
setzt den Spielsatz über die API (`C_LANEPLAYS`) ohne Undo-Block, damit das Rad den
Undo-Verlauf nicht füllt. A/B ist das Zurück für „wer spielt“.

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

**Gruppen (entschieden 30.09.):** keine eigene Einstellung, Rea-Sixty folgt REAPERs
Gruppierung. Steppen, Spielsatz, Comp here und Malen wirken auf alle Spuren derselben
Media-Edit-Gruppe (alle 128 Gruppen, `trackGroups_`), genau dann, wenn REAPERs Gruppierung
eingeschaltet ist: dieselbe Bedingung, die der UF1-Razor schon benutzt (Schalter 1156
„Toggle item grouping override“ und 40771, siehe `uf1RazorCreateAtCursor_` und
`uf1SelectGroupMates_` in main.cpp). Ist sie aus,
wirkt alles nur auf die fokussierte Spur. REAPERs eigene Comp-Area-Actions respektieren die
Gruppierung ohnehin (whatsnew.txt).

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

### Baustein D: „Lanes on the strips“, die Option des Encoder-Modus Lanes

**Entschieden (Frank 30.09.):** kein eigener Modus und kein neuer Name. Die acht Strips
gehören zum Encoder-Modus Lanes (Baustein G), als **Option dieses Modus, ab Werk aus**.
Vorbild ist der UF1: dort nimmt der Jog-Modus Items auf Wunsch den Fader mit
(„Fader = Item Volume“, eine Option pro Jog-Modus, `g_uf1JogFader`). Ein Encoder-Modus, der
auf Wunsch die Strips mitnimmt, ist dasselbe Muster auf dem UF8.

- **Option aus (Werk):** die Strips bleiben Spuren, SEL wählt die Spur, die Top-Keys zeigen
  deren Lanes (Bank „Lanes“), Encoder und Kreuz steppen und comppen. Das ist der Alltag beim
  Vocal-Comping: Lanes hören und zwischendurch die Spur wechseln (Fall 1, Fall 13).
- **Option an:** acht Strips = acht Lanes der fokussierten Spur. Für Versionen und das
  Schnittpult (Fall 4, Fall 6). Um die Spur zu wechseln, schaltet man die Option aus oder
  wählt am UF1 bzw. mit der Maus.
- **Umschalten:** in den Einstellungen des Modus und mit dem Builtin
  `lanes_on_strips_toggle`, damit es mitten in der Arbeit auf einer Taste liegt.
- **Verlassen:** wer den Encoder-Modus verlässt (Karussell, Modus-Taste, Builtin), bekommt die
  Strips immer zurück, egal wie die Option steht. Die Rückgabe läuft über denselben Weg wie
  beim Nav-Overlay (`g_pageDirty`, `g_bankDirty`, `g_sync->invalidate()`).
- **Deutlich zeigen, dass die Fader jetzt Lanes sind:** Banner „Lanes • Strips“, Farbbalken
  nach Spielsatz statt Spurfarbe, Wertzeile „Lane“ auf jedem Strip. Ein Fader, der plötzlich
  etwas anderes fährt, darf nicht unbemerkt passieren.

Gebaut nach dem Vorbild der Send-Ansicht „This Track“ (`StripRoute`, die einzige Stelle, an
der schon heute alle acht Strips Unterobjekte einer Spur zeigen). Folgt dem Fokus, Bank ◄ ►
blättert bei mehr als acht Lanes.

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
  UF1-Soft-Keys und Lanes auf den Strips. Auf dem UF1 zusätzlich „1/7“, „3 von 7“ oder „alle 7“.
- **Einzelne Lane rein/raus:** Shift auf der Bank-Taste, SOLO bei Lanes auf den Strips, Shift+Mitte
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
3. **Acht Lanes als Schnitttasten (UF8).** Mit „Lanes on the strips“ und Malen an ist jedes SEL eine
   Schnitttaste: drücken, und ab jetzt kommt diese Lane in den Comp. Acht Takes nebeneinander,
   Namen auf den Scribbles, die spielende hell. Das kann keine andere Surface.

Gemeinsame Regeln fürs Malen:
- Schnittpunkte rasten optional am Raster (Einstellung „Snap paint to grid“, Werk aus), sonst
  am Play-Cursor. Ein kleiner Vorlauf (Einstellung, z. B. 20 ms) setzt den Schnitt vor die
  Detente, damit Konsonanten nicht abgeschnitten werden.
- Überblendungen macht REAPER selbst (Option „Auto-crossfade when comping“, 42631); Rea-Sixty
  schaltet sie nicht um.
- Ein Strich = ein Undo-Schritt. Live-Comping: ein Undo-Schritt pro Schnitt (Entscheid 6,
  gemessen: jedes 42475 ist ein eigener Eintrag „Create fixed lane comp area“).
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
   mitbringen, vorerst nur Lanes (6 IDs: Kreuz und ENC PUSH; H erweitert das auf alle Modi). Alle anderen Modi fallen auf die
   Zoom-Grundbelegung zurück, für sie ändert sich nichts. Weitere Modi können später je fünf
   IDs dazubekommen.
3. **Dieselbe Tabelle wie am UF1**, dieselben Builtins, derselbe Kern: Encoder = Lane steppen,
   Shift + Encoder = Comp-Area steppen, ↑ / ↓ Lane, ← / → Ränder, Shift + ← Loop,
   Shift + → A/B, Mitte (FIT) tippen = Comp here, Mitte halten + Encoder = malen, Mitte lang =
   Live-Comping. Wer beide Flächen hat, hat an beiden dieselben Griffe.
4. **Anzeige ohne grosses Display:** die Top-Soft-Keys zeigen die Bank „Lanes“ (acht Namen,
   Lampen = Spielsatz). Beim Steppen zeigt die Wertzeile des fokussierten Strips „Lane“ und
   den Namen der gehörten Lane (je 8 Zeichen), beim Malen „Paint“. Der Lanes-Modus holt die
   Bank „Lanes“ selbst auf die Top-Keys und gibt beim Verlassen die vorige zurück
   (entschieden; abschaltbar mit „Lanes mode brings the Lanes bank“).
5. **Encoder-Auflösung:** der Kanal-Encoder schickt mehrere Ereignisse pro Rastung; Lanes
   benutzt denselben Sammler wie Channel Select (`kChannelEncoderScale`), eine Rastung = eine
   Lane.
6. **UF8 allein reicht:** mit G (und D, der Strips-Option) ist der ganze Comping-Ablauf ohne UF1
   machbar. Mit UF1 teilen sich beide denselben Zustand, weil der Spielsatz in REAPER selbst
   steht (`C_LANEPLAYS`) und A/B pro Spur gemerkt wird.

### Baustein H: Modus-Wahl am UF8, Dropdown im Editor und Karussell am Gerät

Mit G bekommt der UF8-Encoder eine Belegung pro Modus. Dann braucht er auch, was der UF1 dafür
hat. Alles davon gibt es beim UF1 schon und wird geteilt, nicht nachgebaut:

- **UF1-Jog:** Ring mit Reihenfolge und Sichtbarkeit (`g_uf1JogSeq`, `g_uf1JogVisible`),
  Karussell SCRUB halten + Rad (`uf1JogModeStep_`), und der Kreuz-Editor folgt dem Live-Modus
  (SettingsScreen.cpp:210, „switching there switches the surface“).
- **UF1-Kanal-Encoder:** eigener Ring über dieselben `EncoderMode`-Werte wie der UF8
  (`g_uf1EncoderSeq`, main.cpp:6909), Karussell MODE halten + Encoder drehen, Editor für
  Reihenfolge und Sichtbarkeit unter dem Binding-Editor von ENC PUSH (SettingsScreen.cpp:5261).

**1. Dropdown im Binding-Editor (UF8).** Wählt man eine Taste des Zoom-Pads (oder ENC PUSH),
steht darüber ein Dropdown „Encoder mode“, wie der Jog-Modus-Wähler beim UF1-Kreuz:
- Es folgt dem Live-Modus, und ein Wechsel dort schaltet den UF8 um. Was man bearbeitet, ist
  immer das, was das Gerät gerade tut.
- Damit jeder Modus im Dropdown bearbeitbar ist, bekommt jeder Modus seine sechs IDs: fünf
  fürs Kreuz, eine für ENC PUSH tippen (bei 17 Modi 102; ButtonId ist 16 Bit). Wie beim UF1 füllt das Laden jede leere Modus-Belegung mit
  der Zoom-Grundbelegung (Muster `fillDerivedUf1Slots_`), und nur Lanes bekommt ab Werk sein
  eigenes Kreuz. Für alle bestehenden Modi zoomt das Kreuz also weiter, bis man es umbelegt.
- Das ändert G: nicht mehr „nur Lanes bekommt IDs“, sondern alle, mit Rückfall wie beim UF1.
  Das Bindings-Upgrade legt sie an.

**2. Karussell am UF8.** Mit dem Encoder den Encoder-Modus wählen, wie SCRUB + Rad am UF1:
- **Griff:** ENC PUSH halten und den Kanal-Encoder drehen. Loslassen ohne Drehen löst aus, was
  im aktuellen Modus auf ENC PUSH liegt (entschieden: pro Modus belegbar; ab Werk
  Plug-in-Fenster wie heute, im Lanes-Modus Comp here). Dieselbe Regel wie bei der Mitte im
  Lanes-Modus: wurde gedreht, war es eine Wahl, und das Loslassen feuert nichts.
- **Eigener Ring für den UF8**, getrennt vom UF1-Encoder-Ring, aber derselbe Code: der Ring
  (Reihenfolge, Sichtbarkeit, Speichern, Schritt) wird aus den UF1-Globals in eine kleine
  Struktur gezogen, die es zweimal gibt (UF1, UF8). So bleiben beide Flächen einzeln
  einstellbar, und es gibt keine zweite Kopie der Logik.
- **Ab Werk alle 17 Modi sichtbar** (entschieden 30.09.); man hakt ab, was man nicht braucht.
- **Reihenfolge und Sichtbarkeit einstellbar**, im selben Editor-Block wie beim UF1, angezeigt
  unter dem Binding-Editor von UF8 ENC PUSH: Häkchen = im Karussell, ▲ / ▼ = Reihenfolge.
- **Anzeige ohne grosses Display:** solange ENC PUSH gehalten ist, zeigen die acht
  Scribble-Strips die Modi des Rings rund um den aktuellen (Fenster wie beim Nav-Overlay),
  der gewählte in der Mitte hell, Farbbalken als Markierung. Loslassen gibt die Strips
  zurück (`g_pageDirty` / `g_bankDirty`, wie das Nav-Overlay). Dazu der Banner
  „Encoder • <Modus>“ und die bestehende `mode_ring`-Liste für das Focused Panel.
- Die Encoder-Modus-Tasten (NUDGE, FOCUS) und die `encoder_*`-Builtins bleiben, wie sie sind;
  das Karussell ist ein zusätzlicher Weg, kein Ersatz.

**Nur genannt, nicht Teil dieses Plans:** Mit dem Kreuz-pro-Modus-Muster könnten später auch
die UF1-Jog-Modi (Playhead, Scrub, Items, Envelope, Razor, Fades) als UF8-Encoder-Modi
kommen. Dann kann ein UF8 ohne UF1 editieren.

### Builtins (alle als Builtin, Kategorie „Lanes“, REAPER-Actions nur auf Wunsch)

`lane_next`, `lane_prev`, `lane_ab`, `lane_play_all`, `lane_play_none`, `lane_play_comp`
(nur aktive Comp-Lane), `lane_play_toggle` (gehörte Lane in den Satz / heraus),
`lane_paint_live` (Live-Comping an/aus), `lane_paint_hold` (Malen, solange gehalten),

`lane_comp_here`, `lane_comp_area_up`, `lane_comp_area_down`, `lane_loop_here`,
`lane_comping_toggle`, `lane_comp_new` (Comp in neue leere Lane), `lane_show_one_toggle`
(`C_LANESCOLLAPSED`), `lanes_fixed_toggle` (`I_FREEMODE`), `jog_mode_lanes`,
`lanes_on_strips_toggle`, `encoder_lanes`. Alle auf die fokussierte Spur (plus Gruppe, wenn
eingestellt).

### Banner, Einstellungen

- Banner: „Jog • Lanes“ (UF1), „Encoder • Lanes“ (UF8), „Lanes • Strips“ an / aus,
  Live-Comping „Lanes • Live paint“
  an / aus. Beim Steppen kein Banner (zu laut), der UF1 zeigt es.
- Einstellungen (Behaviour → Lanes): „Lane steps skip comp lanes“ (Werk an), „Lanes mode brings the
  Lanes bank“ (UF8-Top-Keys, Werk an), „UF1 fader = take gain in Lanes mode“ (Werk aus), „Scrub while
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

### Gemessen am 01.10.2026 (REAPER 7.81, Sonde `rea_sixty_lanes_sonde.lua`)

| Frage | Ergebnis |
|---|---|
| 1 | `LANEREC` Feld 2 = Comp-Lane (0-basiert), -1 = Comping aus. Comp-Lane hiess „1“, nicht „C1“. Comp-Areas stehen als `LINKEDLANE start ende quell-lane 0 -1 0.01 0.01`. |
| 2 | Comping an: Comp-Lane `C_LANEPLAYS` 1, Quellen 0. Mehrere spielende Lanes (Cmd-Klick) je 2. `LANESOLO` war anfangs die verdrehte Maske (4294967281), danach eine normale Bitmaske: nur `C_LANEPLAYS` lesen. |
| 3 | 42481/42482 wirken nur auf ausgewählte Spuren (ohne Auswahl keine Änderung). Von zwei spielenden Lanes (1, 2) ging „next“ auf Lane 2 allein. Ob Comp-Lanes übersprungen werden: nicht geklärt, für uns egal (eigenes Steppen über `C_LANEPLAYS`). |
| 4 | 42707 mit ausgewähltem Comp-Lane-Item: genau diese Area eine Lane hoch (3 → 2). 41082: ALLE Areas der Spur eine Lane hoch, mit Umlauf an der Comp-Lane vorbei (1 → 3). |
| 5 | Lane i von n: y von i/n bis (i+1)/n, gleich wie `F_FREEMODE_Y/H` der Items. 42475 nimmt genau diese Lane als Quelle; Nachbar-Areas werden gekürzt, 10 ms Überlappung. |
| 6 | Kein Rang-Attribut in der API; keine Rang-Zeile im Item-Chunk eines unbewerteten Takes. |
| 7 | 42475 während Play: Area entsteht, kein Aussetzer (Frank), Aufruf 0.1 ms. |
| 8 | Nur der Zustand gemessen (siehe 2), Comping bei Schichtung nicht. |
| 9 | Teilen + `I_CURTAKE` in einem Undo-Block: genau ein Eintrag. |
| Undo | Jede Lane-Action ein Eintrag („Change lane play state“), jedes 42475 ein Eintrag („Create fixed lane comp area“), 42707/41082 je „Edit fixed lane comp area“. |

**Nur genannt:** 41082 / 41083 wären ein eigenes Verb, „ganzen Comp eine Lane hoch / runter“
(z. B. Shift auf einem Bank-Key der Bank „Lanes“). Nicht im Plan, solange Frank es nicht will.

---

## 5. Reihenfolge

1. Sonde (Abschnitt 4).
2. Kern `LaneModel.h` + ctest.
3. Baustein H zuerst in seinem Unterbau: Encoder-Ring in eine geteilte Struktur ziehen (UF1
   verhält sich danach gleich, ctest), Kreuz-IDs pro UF8-Encoder-Modus mit Rückfall, Dropdown
   im Editor, Karussell am UF8. Nützt auch ohne Lanes: jeder Encoder-Modus kann sein Kreuz
   bekommen.
4. Baustein B + E + G, der Lanes-Modus mit Spielsatz auf UF1 (Rad) und UF8 (Encoder, Kreuz
   pro Modus) zusammen, weil beide dieselben Builtins und dieselbe Tabelle benutzen. Samt
   Nav-Werksbelegung, Banner, Handbuch.
5. Baustein F.1 Malen mit dem Rad, dann F.2 Live-Comping.
6. Baustein C, dynamische Bank „Lanes“.
7. Baustein D, die Option „Lanes on the strips“, mit F.3 (SEL als Schnitttasten).
8. Takes in Items im Lanes-Modus.

Jeder Schritt ist für sich nutzbar. B allein ist schon das, was es nirgends gibt.

---

## 6. Entscheide für Frank

**Alles entschieden (Frank 30.09.2026):**
- Lanes ist ein Encoder-Modus (UF8) bzw. Jog-Modus (UF1), kein neuer Modus-Typ; die Strips
  sind eine Option dieses Modus, ab Werk aus (Baustein D).
- Encoder-Modus wählen per ENC PUSH halten + drehen bleibt („sehr geil, behalten“).
- Mithören beim Malen mit dem Rad ist optional, ab Werk aus.
1. Reihenfolge wie in Abschnitt 5: die Strips-Option kommt spät.
2. UF8-Fader bei „Lanes on the strips“ = Take-Gain des Items dieser Lane unter dem Cursor.
3. Gruppen: keine eigene Einstellung, Rea-Sixty folgt REAPERs Gruppierung (Media-Edit-Gruppen,
   wirksam, wenn REAPERs Gruppierung eingeschaltet ist, wie beim UF1-Razor).
4. Rad / Encoder in der Grundstellung steppen die ganze Spur; Shift = Comp-Area.
5. Mitte des Kreuzes im Lanes-Modus: tippen = Comp here, halten + drehen = malen, lang =
   Live-Comping.
6. Live-Comping schreibt jeden Lane-Wechsel sofort; jeder Schnitt ist ein Undo-Schritt
   (Frank 01.10., nach der Sonde: ein Undo-Block über einen ganzen Durchgang ist bei sofortigem
   Schreiben nicht zu haben).
7. Takes in Items kommen mit, als letzter Schritt.
8. UF8 ENC PUSH tippen ist pro Encoder-Modus belegbar; ab Werk Plug-in-Fenster, im
   Lanes-Modus Comp here.
9. Der Lanes-Modus holt die Bank „Lanes“ auf die UF8-Top-Keys und gibt die vorige zurück,
   abschaltbar.
10. UF8-Karussell ab Werk mit allen 17 Modi sichtbar.

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
   oder runter. Technisch: das Comp-Lane-Item unter dem Cursor auswählen, 42707 / 42708
   (gemessen: verschiebt genau diese eine Area, Quell-Lane 3 → 2).
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

**Ausgangslage:** Spur „Solo Gtr“, fünf Takes in Lanes 1 bis 5. UF8 im Encoder-Modus Lanes,
„Lanes on the strips“ an: acht Strips, davon fünf belegt, Namen auf den Scribbles.

1. **Mitte lang** (UF1) oder das Builtin `lane_paint_live` auf einem Soft-Key: Live-Comping an.
   Banner „Lanes • Live paint“, die Mitte leuchtet.
2. **Play** vor dem Solo.
3. **SEL auf Strip 2** in Takt 1 des Solos, **SEL auf Strip 5** in Takt 5, **SEL auf Strip 1**
   für die letzte Phrase. Jede SEL: Rea-Sixty merkt Zeitpunkt und Lane; ab da hört man diese
   Lane (exklusiv). Der Farbbalken der gewählten Lane leuchtet hell.
4. Jeder SEL schreibt sofort (entschieden): die Abschnitte [Start → Takt 5: Lane 2],
   [Takt 5 → letzte Phrase: Lane 5] entstehen im Arrange, während das Solo läuft. **Stop**
   schliesst den letzten Abschnitt [letzte Phrase → Stop: Lane 1] ab. Jeder Schnitt ist ein
   Undo-Schritt, hier also drei.
5. **Mit Schleife:** Loop über das Solo, Live-Comping an. Jeder Durchgang schreibt seine
   Wechsel über den vorigen; wenn es sitzt, Live-Comping aus.

Ohne UF8 geht dasselbe mit dem Rad oder ↑ / ↓ auf dem UF1: jeder Lane-Schritt ist ein Schnitt.

### Fall 5: Drums in der Gruppe comppen

**Ausgangslage:** Kick, Snare, OH L/R, Room, alle in Media-Edit-Gruppe 3, jede mit vier Takes
in Lanes 1 bis 4. REAPERs Gruppierung ist eingeschaltet.

1. **SEL auf Snare**, Jog-Modus Lanes.
2. **Rad:** jeder Schritt schaltet auf allen fünf Spuren dieselbe Lane (Gruppe über alle 128
   Gruppen, `trackGroups_`). Man hört das ganze Set aus Take 3.
3. **Fill in Takt 16 aus Take 3:** Zeitauswahl über den Fill (am Rechner oder mit dem
   Razor-Modus), **Mitte tippen**. Rea-Sixty setzt den Razor auf Lane 3 aller Gruppenspuren,
   42475 legt auf allen fünf die Comp-Area an. Phasengleich, weil alle Spuren dieselbe Strecke
   aus derselben Lane bekommen.
4. **Eine Spur allein** (Room nur aus Take 1): REAPERs Gruppierung kurz aus, Room fokussieren,
   comppen, Gruppierung wieder an.

### Fall 6: Drei Bass-Versionen (Playlist-Betrieb)

**Ausgangslage:** Spur „Bass“, Lane 1 „Finger“, Lane 2 „Pick“, Lane 3 „Synth“, jede eine ganze
Fassung, exklusiv. Kein Comping.

1. **Encoder-Modus Lanes** (ENC PUSH halten + drehen), dann **`lanes_on_strips_toggle`**
   (Taste nach Wahl). Banner „Lanes • Strips“. Strips 1 bis 3 heissen Finger, Pick, Synth;
   Finger hell.
2. **Play, SEL auf „Pick“.** Nur Lane 2 spielt, im Takt, ohne Aussetzer. SEL „Synth“,
   SEL „Finger“: Versionen im Kontext des Songs vergleichen.
3. **Fader auf „Synth“ ziehen** (Fader = Take-Gain unter dem Cursor): die Synth-
   Fassung ist 3 dB lauter, runter, damit der Vergleich fair ist. Das ändert das Item in Lane 3
   unter dem Cursor, nicht die Spur.
4. **A/B** (Shift + → auf dem UF1): zwischen den zwei zuletzt gehörten Fassungen hin und her.
5. Entschieden: „Pick“ bleibt die spielende Lane und wird so gespeichert.

### Fall 7: Gedoppelte Gitarre, mehrere Lanes zugleich

**Ausgangslage:** Spur „Rhythm Gtr“, Lanes 1 bis 4 sind vier Einspielungen. Gewollt: 1 und 3
zusammen als Dopplung.

1. **Bank „Lanes“** (UF1-Soft-Keys oder UF8-Top-Keys): Druck auf Lane 1 = nur Lane 1.
   **Shift + Druck auf Lane 3** = Lane 3 dazu. Beide Tasten hell, der UF1 zeigt „2 von 4“.
2. Dasselbe mit Lanes auf den Strips: SEL auf 1, SOLO auf 3.
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
Kommt als letzter Schritt (entschieden).

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
- Lanes auf den Strips: Strips 1 bis 8, **Bank ◄ / ►** zeigt 9 bis 14.
- Das Rad kennt keine Seiten: es steppt durch alle 14, und die Bank blättert von selbst mit, damit
  die gehörte Lane immer auf einer Taste zu sehen ist.

### Fall 11: Spur ohne Lanes

**Ausgangslage:** Lanes-Modus an, fokussiert ist eine normale Spur mit einem Take.

- UF1: „No lanes“ in der Kopfzeile, Rad und Kreuz tun nichts, kein Fehler.
- Bank „Lanes“: alle Tasten dunkel, bis auf eine: „Lanes on“ (`lanes_fixed_toggle`).
- Lanes auf den Strips: Strips leer, SEL tut nichts. Die Option bleibt an, damit sie auf der
  nächsten Spur mit Lanes wieder greift.
- Sobald man eine Spur mit Lanes fokussiert, ist alles wieder da. Kein Modus springt von selbst
  um.

### Fall 12: Einen Durchgang verwerfen

- **Comp here / Malen:** jeder Strich ist ein Undo-Schritt, REAPERs Undo nimmt ihn zurück.
- **Live-Comping:** geschrieben wird sofort, jeder Schnitt ist ein Undo-Schritt. Einen ganzen
  verpatzten Durchgang nimmt man mit so vielen Undo zurück, wie er Schnitte hatte.
- **Spielsatz:** Rea-Sixty legt beim Steppen keinen Undo-Schritt an; A/B ist das Zurück für
  „wer spielt“.

### Fall 13: Comping nur mit dem UF8

**Ausgangslage:** kein UF1. Spur „Lead Vox“ wie in Fall 1, UF8 Top-Keys auf Bank „Lanes“.

1. **SEL auf „Lead Vox“**, dann Encoder-Modus Lanes (ENC PUSH halten + drehen, oder
   `encoder_lanes` auf einer Taste). Banner „Encoder • Lanes“; das Zoom-Pad ist jetzt das
   Lanes-Kreuz. „Lanes on the strips“ ist aus (Werk): die Strips bleiben Spuren, man kann
   jederzeit mit SEL eine andere Spur fokussieren, und die Top-Keys zeigen deren Lanes.
2. **Shift + ←** (Zoom-Pad) = Loop auf die Zeile, **Play**.
3. **Kanal-Encoder drehen:** Take für Take; die Top-Key-Lampe wandert mit, die Wertzeile des
   Vox-Strips zeigt „Lane“ und den Namen.
4. **FIT (Mitte) tippen:** Comp here, wie Fall 1 Schritt 5.
5. **FIT halten und Encoder drehen:** malen, wie Fall 3.
6. **Zurück zum Mischen:** `encoder_nudge` oder die Modus-Taste, die man gewohnt ist. Das
   Zoom-Pad zoomt wieder, die Top-Keys sind wieder auf ihrer vorigen Bank.

### Fall 14: Encoder-Modus am UF8 wählen und das Kreuz umbelegen

**Ausgangslage:** UF8, Encoder auf Channel Select, Kreuz zoomt.

1. **ENC PUSH halten, Encoder drehen.** Die acht Scribbles zeigen die Modi des Rings, der
   aktuelle in der Mitte hell; jede Rastung schiebt den Ring eins weiter. Banner „Encoder •
   Lanes“, als Lanes in der Mitte steht.
2. **Loslassen.** Der UF8 ist im Modus Lanes, die Scribbles zeigen wieder die Spuren, das
   Kreuz ist das Lanes-Kreuz. ENC PUSH hat nichts ausgelöst, weil gedreht wurde.
3. **Settings → Bindings → UF8, Taste ← des Zoom-Pads anklicken.** Das Dropdown „Encoder
   mode“ steht auf Lanes, weil der UF8 dort ist. Man belegt Shift + ← um. Dropdown auf
   „Markers“ stellen: der UF8 schaltet auf Markers, das Kreuz zeigt dessen Belegung (die
   Zoom-Grundbelegung, bis man dort etwas setzt).
4. **UF8 ENC PUSH im Editor anklicken:** darunter die Liste „Channel encoder modes (UF8)“.
   Mousewheel und Bank by 1 abhaken, Lanes mit ▲ neben Channel Select schieben. Ab jetzt
   springt das Karussell zwischen den verbliebenen Modi in dieser Reihenfolge.


---

## 8. Fallstudien am UF8

Stand nach dem Entscheid vom 30.09.: Lanes ist ein Encoder-Modus, gewählt per ENC PUSH
halten + drehen; das Zoom-Pad ist in diesem Modus das Lanes-Kreuz; die Top-Keys tragen die
Bank „Lanes“; die acht Strips werden nur mit der Option „Lanes on the strips“ zu Lanes.
Der UF8 hat keinen Transport: Play kommt von der Tastatur, einer belegten Taste oder dem UF1.

Die Griffe in allen Fällen:

| Griff | Lanes-Modus (Werk) |
|---|---|
| Kanal-Encoder | gehörte Lane steppen; mit Shift die Comp-Area unter dem Cursor |
| Zoom-Pad ↑ / ↓ | Lane steppen; mit Shift die Comp-Area |
| Zoom-Pad ← / → | vorige / nächste Comp- bzw. Item-Grenze; Shift ← Loop, Shift → A/B |
| FIT tippen | Comp here; mit Shift die gehörte Lane in den Spielsatz / heraus |
| FIT halten + Encoder | malen |
| FIT lang, ohne Drehen | Live-Comping an / aus |
| Top-Key | nur diese Lane; Shift = dazu / weg; lang = Comp-Lane; Cmd = Comp here aus dieser Lane |
| ENC PUSH halten + drehen | Encoder-Modus wählen (Karussell) |

### U1: Vocal comppen, Strips bleiben Spuren

**Ausgangslage:** Session mit 24 Spuren auf dem UF8, „Lead Vox“ hat sechs Takes in Lanes.

1. **SEL auf „Lead Vox“.** Die Spur ist fokussiert.
2. **ENC PUSH halten, Encoder drehen** bis „Lanes“ in der Mitte der Scribbles steht,
   loslassen. Banner „Encoder • Lanes“. Die Scribbles zeigen wieder die Spuren, die Fader
   bleiben Lautstärken. Die Top-Keys zeigen „1“ bis „6“ bzw. die Lane-Namen, Lane 1 hell.
3. **Shift + ←** auf dem Zoom-Pad: Loop auf die erste Zeile. **Play** (Tastatur).
4. **Encoder drehen:** Take für Take, die helle Top-Key-Lampe wandert mit. Die Wertzeile des
   Vox-Strips zeigt „Lane“ und den Namen, z. B. „Take 4“.
5. **FIT tippen:** Take 4 kommt für diese Zeile in den Comp (C1 wird angelegt, die Top-Keys
   bekommen eine Taste mehr, C1 in eigener Farbe).
6. **→**, **Shift + ←**, weiter bei 4. Zwischendurch **SEL auf „Backing Vox“**: die Top-Keys
   zeigen sofort deren Lanes, der Encoder steppt jetzt dort. Kein Moduswechsel nötig.

### U2: Mit den Top-Keys direkt wählen statt steppen

**Ausgangslage:** wie U1, sechs Takes.

1. **Top-Key 5 drücken:** nur Lane 5 spielt, sofort, ohne durch 2 bis 4 zu steppen.
2. **Cmd + Top-Key 3:** „Comp here“ aus Lane 3, ohne sie vorher zu hören. Für den, der schon
   weiss, welcher Take es an dieser Stelle ist.
3. **Top-Key auf C1 lang:** C1 wird die aktive Comp-Lane (bei mehreren Comps: C1 / C2 als
   Varianten eines Comps).

### U3: Eine Silbe malen, stumm

**Ausgangslage:** wie U1, Comp steht, eine Silbe soll aus Take 2 kommen. „Scrub while
painting“ aus, „Play the stroke after painting“ an (Werk).

1. **Encoder** auf Lane 2 (Top-Key 2 leuchtet).
2. Cursor vor die Silbe (← / → an die nächste Grenze, oder Maus).
3. **FIT halten, Encoder langsam nach rechts:** der Cursor fährt stumm, im Arrange wächst
   ein Razor auf der Höhe von Lane 2. Zu weit: zurückdrehen.
4. **FIT loslassen:** die Strecke wird Comp-Area aus Lane 2, REAPER spielt sie mit 1 s
   Vorlauf einmal ab und springt zurück. Ein Undo-Schritt.

### U4: Das Schnittpult, Lanes auf den Strips

**Ausgangslage:** „Solo Gtr“ mit fünf Takes. Lanes-Modus wie in U1.

1. **`lanes_on_strips_toggle`** (auf einer Taste). Banner „Lanes • Strips“. Die Motorfader
   fahren auf die Take-Gains der fünf Lanes (je das Item unter dem Cursor), Strips 6 bis 8
   leer. Scribbles: die Lane-Namen. Farbbalken: Lane 1 hell, der Rest dunkel. Wertzeilen:
   „Lane“.
2. **FIT lang:** Live-Comping an, Banner „Lanes • Live paint“.
3. **Play** vor dem Solo. **SEL 2** in Takt 1, **SEL 5** in Takt 5, **SEL 1** für die letzte
   Phrase. Jeder SEL ist ein Schnitt, der Farbbalken springt mit.
4. Jeder SEL schreibt sofort, die Areas entstehen während des Spielens; **Stop** schliesst
   den letzten Abschnitt ab. Drei Schnitte, drei Undo-Schritte.
5. **`lanes_on_strips_toggle`** wieder aus: die Strips sind wieder Spuren, die Fader fahren
   auf die Spurlautstärken zurück.

### U5: Versionen vergleichen und angleichen

**Ausgangslage:** „Bass“ mit drei ganzen Fassungen: Finger, Pick, Synth. Lanes auf den
Strips an (wie U4, Schritt 1).

1. **Play, SEL „Pick“, SEL „Synth“, SEL „Finger“:** jede Fassung im Song, ohne Aussetzer.
2. **Synth ist zu laut:** Fader von „Synth“ runter (Take-Gain des Items unter
   dem Cursor). Fein nachstellen mit dem V-Pot desselben Strips.
3. **V-Pot-Push auf „Synth“:** Loop auf das Synth-Item unter dem Cursor, zum genauen Vergleich.
4. **Shift + →** (A/B): zwischen den beiden zuletzt gehörten Fassungen hin und her.
5. Entschieden: SEL „Pick“, Option aus. Pick spielt, die Spur ist wieder ein Kanal.

### U6: Dopplung auf zwei Lanes

**Ausgangslage:** „Rhythm Gtr“ mit vier Einspielungen, Lanes auf den Strips an.

1. **SEL 1, dann SOLO 3:** Lanes 1 und 3 spielen zusammen, beide Farbbalken hell, die
   Top-Keys 1 und 3 ebenso.
2. **Encoder eine Rastung:** nur Lane 4 zum Vorhören (exklusiv).
3. **Shift + →:** zurück auf 1 + 3, der ganze Satz.
4. **CUT 3:** Lane 3 aus dem Satz, nur noch 1 spielt.

### U7: Drums in der Gruppe, vom UF8 aus

**Ausgangslage:** Kick, Snare, OH L, OH R, Room in Media-Edit-Gruppe 3, je vier Takes.
REAPERs Gruppierung an. Strips bleiben Spuren (Option aus).

1. **SEL auf „Snare“**, Lanes-Modus. Die Top-Keys zeigen die Lanes der Snare.
2. **Encoder:** alle fünf Spuren schalten mit; man hört das Set aus Take 2, 3, 4.
3. **Zeitauswahl über den Fill** (Maus oder UF1-Razor), **FIT tippen:** der Fill aus der
   gehörten Lane landet auf allen fünf Spuren im Comp, phasengleich.
4. Die Fader bleiben dabei die fünf Drum-Kanäle: man kann beim Comppen die Balance fahren.

### U8: Vierzehn Lanes

**Ausgangslage:** „Lead Vox“ mit 14 Lanes.

- **Option aus:** die Top-Keys zeigen acht Lanes, der Encoder steppt durch alle 14, und die
  Bank blättert von selbst mit, sobald die gehörte Lane ausserhalb liegt. Blättern von Hand
  wie bei der FX-Bank.
- **Option an:** Strips 1 bis 8, **Bank ◄ / ►** zeigt 9 bis 14. Auch hier blättert der
  Encoder mit.

### U9: Zurück zum Mischen

**Ausgangslage:** Lanes-Modus mit Lanes auf den Strips.

1. **ENC PUSH halten, Encoder drehen** bis „Channel Select“, loslassen.
2. Die Strips kommen zurück, egal wie die Option steht: Scribbles = Spuren, Farbbalken =
   Spurfarben, die Fader fahren auf die Spurlautstärken. Das Zoom-Pad zoomt wieder.
3. Die Option bleibt gespeichert: beim nächsten Wechsel in Lanes sind die Strips wieder Lanes.
4. Die Top-Keys gehen zurück auf ihre vorige Bank (ausser „Lanes mode brings the Lanes bank“
   ist aus, dann waren sie gar nicht umgeschaltet).

### U10: UF8 und UF1 zusammen

**Ausgangslage:** beide Flächen, „Lead Vox“ fokussiert. UF8 im Lanes-Modus mit Lanes auf den
Strips, UF1 im Jog-Modus Lanes.

- **UF8 = Übersicht und Direktwahl:** acht Lanes mit Namen, Lampen für den Spielsatz, SEL
  wählt, die Fader gleichen Take-Pegel an.
- **UF1 = Rad und Malen:** Rad steppt, Mitte comppt, Mitte halten + Rad malt, das grosse
  Display zeigt Name und „3/7“.
- Beide zeigen denselben Stand, weil der Spielsatz in REAPER steht: steppt das Rad, springt
  der helle Farbbalken auf dem UF8 mit; drückt man SEL auf dem UF8, steht der neue Name auf
  dem UF1.

### U11: Spur ohne Lanes

**Ausgangslage:** Lanes-Modus, SEL auf „Kick“, die keine Lanes hat.

- Top-Keys dunkel bis auf „Lanes on“. Encoder und Zoom-Pad tun nichts.
- Mit Lanes auf den Strips: Strips leer, Fader in Ruhestellung, SEL tut nichts. Die Option
  bleibt an und greift auf der nächsten Spur mit Lanes.
- **Top-Key „Lanes on“:** die Kick bekommt Fixed Lanes, ab dann gilt alles oben.
