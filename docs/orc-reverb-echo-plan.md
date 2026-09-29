# Plan: Reverb und Echo auf der UF1 (Side-Car und ORC)

Stand 29.09.2026. Nur Plan, nichts gebaut. Gilt für beide Programme, weil ORC den
Side-Car der Extension wortwörtlich fährt (RmeState, RmeStrip, RmeBuiltins, RmeFace).

## Was es schon gibt

**Gemessen** (`docs/session-2026-09-27-orc-settings-cleanup.md`, Mitschnitte 26./27.09.):
- TotalMix sendet auf `/sendall` 14 Reverb-Felder (`enable type predelay lowcut highcut
  attack hold release roomscale time highdamp smooth volume width`), 7 Echo-Felder
  (`enable type delay feedback highcut volume width`), `/input/<n>/fxsend`,
  `/output/<n>/fxreturn`, `/controlroom/mutefx`.
- Die Werte kommen in echten Einheiten: predelay 20 (ms), lowcut 20 und highcut 18000 (Hz),
  roomscale 1.0, time 3.0 (s), smooth 100, volume -3 (dB), width 0.6, echo delay 0.6 (s),
  feedback 20.
- Menüwerte = Position im TotalMix-Menü ab 0 (Tabelle der 15 Reverb-Typen, 3 Echo-Typen,
  6 Echo-High-Cut-Stufen in der Session-Notiz).
- Schreiben: `/echo/enable` geht (Frank sah es). `/playback/<n>/fxsend` geht auch, wird
  aber nie gemeldet. TotalMix meldet der schreibenden Remote nichts zurück.

**RMEs OSC-Tabelle** (`OSCProtocoll_260721.ods`, 2.1 beta 2) nennt nur `/reverb/enable`,
`/echo/enable`, `fxsend`, `fxreturn`, `/mutefx`. Die übrigen Felder sendet TotalMix, die
Tabelle führt sie nicht. Ob TotalMix sie auch annimmt, ist nicht geprüft.

**RMEs UFX+-Handbuch, Kap. 25.6** (rme-audio.de/downloads/fface_ufxplus_e.pdf, gelesen 29.09.):

| Feld | Bereich laut Handbuch | Gilt für |
|---|---|---|
| PreDelay | 0 bis 999 ms | alle Typen |
| Low Cut | 20 bis 500 Hz | alle |
| High Cut | 5 bis 20 kHz | alle |
| Smooth | 0 bis 100 | alle |
| Width | 100 (stereo) bis 0 (mono); OSC meldet 0.6, also 0..1 | alle |
| Volume | nicht angegeben | alle |
| Room Scale | 0.5 bis 3.0 | „Room Types" |
| Attack, Hold, Release | je 5 bis 400 ms | Envelope, Gated |
| Reverb Time | 0.1 bis 4.9 s | Space |
| High Damp | 5 bis 20 kHz | Space |
| Echo Delay Time, Feedback, Volume | nicht angegeben | Echo |
| Echo Width | 100 bis 0, OSC 0..1 | Echo |

**Im Code schon da:**
- `fxsend` steht im STRIP-Katalog und auf der Werksseite „Input" (Pot 2), für Eingänge.
  Playbacks melden ihn nie, darum zeigt der STRIP ihn dort nicht (Regel „was TotalMix
  meldet, wird gezeigt").
- `fxreturn` steht im Katalog (`RmeStrip.cpp`), aber auf **keiner** Werksseite.
- `mutefx` wird gelesen (`RmeState.cpp:105`, `State::muteFx`), aber nichts benutzt es:
  kein Built-in, keine Lampe.
- `Manager::send` spielt jeden eigenen Schreibvorgang sofort in den eigenen Zustand zurück
  (`localEcho`, `RmeManager.cpp:323`). Das deckt das fehlende Echo von TotalMix für
  Reverb/Echo ohne Zusatzarbeit ab.

## Was sich ändert

### Auf der UF1

Vorschlag, **Entscheidung 1**: Reverb und Echo als Seiten am Ende des STRIP, wie EQ oder
Dyn. Die Seiten zeigen auf jedem Kanal dieselben globalen Werte. Blättern, Anzeige,
V-Pot-Druck und Tasten laufen über die bestehende STRIP-Mechanik, kein neuer Modus.

| Seite | Pots 1-4 | Tasten 1-4 |
|---|---|---|
| Reverb | Type, PreDelay, Volume, Width | Reverb an/aus, Echo an/aus, Mute FX |
| Reverb 2 | Low Cut, High Cut, Smooth, Room Scale | Reverb an/aus |
| Reverb 3 | Attack, Hold, Release (Envelope, Gated) oder Time, High Damp (Space) | Reverb an/aus |
| Echo | Type, Delay, Feedback, Volume | Echo an/aus |
| Echo 2 | High Cut, Width | Echo an/aus |

Die Belegung ist ein Vorschlag, sie steht in `rme.json` und lässt sich dort umstellen.
Die Seiten sind Layout 1, kein EQ-Graph.

**Entscheidung 2:** „Reverb 3" nur zeigen, wenn der gewählte Typ diese Regler hat? Oder
immer alles zeigen? Das Handbuch sagt, welche Regler zu welchem Typ gehören. Ob TotalMix
sie bei anderen Typen ausblendet, weiss ich nicht.

**Entscheidung 3:** FX Return für Ausgänge. Die Werksseite „Output" ist voll. Eine Seite
„Output 2" mit FX Return auf Pot 1?

**Mute FX als Built-in** `rme_mute_fx`, wie Dim und Mono (`RmeBuiltins.cpp`), mit Lampe,
umschaltend (Switch). **Entscheidung 4:** auf eine Werkstaste legen, oder nur im Picker
anbieten?

**Playback-FX-Send bleibt verborgen.** TotalMix meldet ihn nie, der STRIP würde einen
Wert zeigen, den niemand kennt.

### Anzeige (UF1 Textfelder = 8 Zeichen)

Kurzformen, **Entscheidung 5** (Vorschlag):
- Reverb-Typen: `Small`, `Medium`, `Large`, `Walls`, `Shorty`, `Attack`, `Swagger`,
  `OldSchl`, `Echoist`, `8plus9`, `GrandWd`, `Thicker`, `Envelope`, `Gated`, `Space`
- Echo-Typen: `Stereo`, `Cross`, `Pong`
- Echo High Cut: `off`, `16k`, `12k`, `8k`, `4k`, `2k`
- Labels: `Rev Type`, `PreDelay`, `Volume`, `Width`, `Low Cut`, `High Cut`, `Smooth`,
  `RoomScl`, `Attack`, `Hold`, `Release`, `Time`, `HiDamp`, `EchoType`, `Delay`,
  `Feedback`, `Reverb`, `Echo`, `Mute FX`

## Dateien und Funktionen

| Datei | Änderung |
|---|---|
| `RmeState.h/.cpp` | `State::fx` (Blattname nach Wert, wie `Channel::leaves`), `ingest` nimmt `/reverb/*` und `/echo/*` |
| `RmeStrip.h/.cpp` | `Param` bekommt `global`: Adresse `/` + Blatt statt `/<section>/<n>/<blatt>`, Wert aus `State::fx`, vorhanden wenn TotalMix das Feld gemeldet hat (plus Typregel, Entscheidung 2). Neue Katalogeinträge `rev_*`, `echo_*`. Zwei neue Arten: Prozent (Smooth, Width 0..1 als 0..100, Feedback) und Faktor (Room Scale). `writesFor`, `value`, `available`, `format`, `norm`, `nudge`, `press` lernen `global`. |
| `RmeManager.cpp` | Werksseiten ergänzen. `rme.json` Version 5: Eine vorhandene `strip`-Liste ersetzt die Werksseiten heute vollständig (`RmeManager.cpp:177-198`). Ohne Hochstufung kommen die neuen Seiten bei niemandem an, der die Datei schon hat, auch bei Frank nicht (`ORC/rme.json` und `REAPER/rea_sixty/rme.json`). Die Hochstufung hängt die FX-Seiten einmal hinten an, wenn keine davon da ist. |
| `RmeBuiltins.cpp` | `rme_mute_fx` über `regRmeCr` + `Manager::ControlRoom` bekommt `muteFx`. `setBuiltinKind(..., Switch)`. |
| Picker-Kategorie | Pflicht für jedes neue Built-in, CI prüft (`check_builtin_docs.py`). |
| `tests/test_rme_osc.cpp` | `ingest` der FX-Felder, Adressen der Schreibvorgänge, Hochstufung v4 auf v5 genau einmal, Sichtbarkeit nach Typ. |
| `docs/user-manual.md` | Side-Car-Kapitel: die neuen Seiten, Mute FX. |
| Changelog | Eine Zeile. |

**Wer fasst die Werte sonst noch an:** `State::fx` wird nur von `ingest` geschrieben
(TotalMix und `localEcho`) und nur vom STRIP gelesen. `State::muteFx` schreibt `ingest`,
lesen wird nur das neue Built-in.

## Messen, bevor gebaut wird (Frank, am TotalMix)

1. **Nimmt TotalMix die Felder an?** Die Tabelle nennt nur `enable`. Pro Feld einmal
   schreiben (z.B. `/reverb/predelay 50`) und in TotalMix schauen. Bis das belegt ist,
   werden nur die Felder gebaut, die TotalMix annimmt.
2. **Fehlende Bereiche:** Reverb Volume, Echo Delay, Echo Feedback, Echo Volume. Mit
   `ORC_TRACE=1` jeden Regler in TotalMix an beide Enden ziehen, der Mitschnitt zeigt die Werte.
3. **Für Entscheidung 2:** zeigt TotalMix bei Envelope, Gated und Space andere Regler?
   Ein Bildschirmfoto des FX-Fensters pro Gruppe reicht.

## Was ich nicht baue

- Playback-FX-Send (nicht lesbar).
- Echo in BPM (TotalMix sendet nur Sekunden).
- Presets von TotalMix.
- Reverb/Echo auf dem UF8.
- Nichts im ORC-Einstellungsfenster: Die STRIP-Seiten haben dort keinen Editor, nur `rme.json`.
