# Inserts active-FX marker → grafisches Overlay (Spike)

Ziel: das Feature *„Active CS / BC marker in REAPER's TCP/MCP Inserts list"*
von **FX-Rename** (heute: Glyph-Präfix in `renamed_name`, siehe
`main.cpp` → `reconcileInsertMarkersForTrack_`) auf ein **nicht-destruktives
Grafik-Overlay** umstellen. Kein verändertes `renamed_name`, kein „dirty"-
Projekt — die vom Surface getriebene CS/BC-Instanz wird direkt in der
Insert-Liste grafisch hervorgehoben (heller Rahmen / Glow).

## Warum nicht „nur Theme"

REAPER bietet **keine** per-FX-Farbe/Helligkeit in der Insert-Liste. Die
Theme-Farbe für FX-Text ist global; pro-FX unterscheidet REAPER nur
*bypassed* / *offline* — und die ändern Audio. Ein reiner Theme-Tweaker-Weg
kann also keine einzelne Insert-Zeile markieren.

## Inspiration & tatsächlicher Mechanismus

Der **WT Graphical Annex** (White Tie) beweist, dass grafische Layer in
REAPERs UI etabliert sind — er ist aber ein **angedocktes gfx-Spiegel-Panel**
(`gfx.dock`) und zeichnet *nicht* auf den nativen Mixer. Direkt auf die native
UI zeichnet man mit **`JS_Composite`** (js_ReaScriptAPI) + LICE-Bitmap,
positioniert über **`GetThingFromPoint`**-Reverse-Hit-Test. Genau diese Kette
validiert der Spike.

Unser Vorteil ggü. einem reinen Script: die Extension **kennt die aktive
CS/BC-Instanz schon** (`uc1::csInstanceIndex` / `bcInstanceIndex`,
`onInstanceCursorChanged_`). Das Companion-Overlay muss den Zustand nur lesen,
nicht selbst ermitteln.

## Der Spike

`analysis/spike_inserts_overlay.lua` — als Action ausführen, dann mit der Maus
über die **FX-Insert-Liste im Mixer (MCP)** und im **TCP** fahren. Das kleine
gfx-Fenster zeigt einen Live-Readout; die Console loggt jede erkannte Zeile.
Dummy-State: markiert wird die Zeile **unter der Maus** (im echten Feature
kommt der Index von der Extension).

Er prüft die zwei offenen Fragen:

1. **Zeilen-Granularität** — liefert `GetThingFromPoint` pro FX-Zeile eine
   unterscheidbare Kennung (`fx_0` / `fx_1` / …)? Der Scan leitet daraus das
   Zeilen-Rechteck ab.
2. **Compositing** — legt `JS_Composite` ein Highlight passgenau auf die
   Zeile über den nativen Mixer (und entfernt es sauber)?

## Auswertung → Entscheidung

- **`isFx = JA`, Zeilen-Rect ist pro Insert verschieden, Overlay sitzt
  passgenau** → Vollausbau wie geplant: Extension publiziert Active-State via
  `ExtState`, gebundeltes Companion-Lua zeichnet via `JS_Composite`.
- **`isFx = nein` oder Rect umfasst den ganzen Track/FX-Block** →
  `GetThingFromPoint` ist für Zeilen zu grob. Fallback: Zeilenhöhe aus
  `CountTCPFXParms` / Theme-Layout (`mcp.fxlist`) ableiten und Zeilen relativ
  zum MCP/TCP-Rect (`I_MCPY/H`, `I_TCPY/H`) rechnen.
- **`JS_Composite` fehlt** (kein js_ReaScriptAPI) → Abhängigkeit dokumentieren
  oder Compositing nativ aus der Extension (LICE) erwägen.

## Ergebnis (2026-06-12, REAPER 7.74 macOS)

Capture: `analysis/captures/gtfp-info-strings-reaper774.md` (+ Roh-Log
`gtfp-raw-reaper774.log`).

- **Caps: alle OK** — `JS_Composite`, `JS_Window_FromPoint`,
  `JS_Window_GetClientRect`, `JS_LICE_*`, `GetThingFromPoint`,
  `GetTrackFromPoint`. Der Overlay-Weg ist auf Frank's System voll verfügbar,
  kein Compositing-Fallback nötig. Overlay rendert ("das ist gold").
- **Frage 1 (Zeilen-Granularität): NEIN.** `GetThingFromPoint` liefert die
  Insert-Liste als **einen Block** `mcp.fxlist` (rect = ganze Liste, z.B.
  84×228), **ohne FX-Index**. Die `fx_1`-Hits waren offene Plugin-Fenster, nicht
  die Liste. TCP zeigt bei Frank keine FX-Namensliste (nur `tcp.fx`/`tcp.fxparm`
  Knöpfe + eingebettete Params).
- **Frage 2 (Compositing): JA** — Highlight legt sich via `JS_Composite` auf das
  native Mixer-Fenster.

→ **Entscheidung: FALLBACK-Pfad bauen.** GetThingFromPoint findet den
**fxlist-Block**, die einzelne **Zeile rechnen wir selbst**.

## Vollausbau-Plan (beschlossen)

1. **State, nicht Rename:** Extension publiziert die aktive CS/BC-Instanz via
   `ExtState` (TrackGUID + fxIdx) statt `renamed_name` zu schreiben. Quelle:
   `onInstanceCursorChanged_` (kennt CS/BC-Instanz schon). Kein dirty-Projekt.
2. **Companion-Lua** (gebündelt, ReaPack-`@provides`) liest den State im
   `defer`-Loop und zeichnet via `JS_Composite`.
3. **Strip-Rect mausfrei:** `GetMediaTrackInfo_Value(tr, "I_MCPX/Y/W/H")` bzw.
   `I_TCPY/H` → Position jedes Track-Strips ohne Maus. fxlist-Offset im Strip
   einmalig per Scan-Technik (aus dem Spike) kalibrieren — Theme-konstant.
4. **Zeile N** = fxlist-Top + N · Zeilenhöhe. Zeilenhöhe pro Theme/Zoom
   kalibrieren (≈ `tcp.fxparm`-Höhe 21px bei diesem Theme), nicht hartkodieren.
5. **Ziel-HWND** via `JS_Window_FromPoint` am Zeilenpunkt — es gibt zwei
   Mixer-Fenster (angedockt `0x108…` + separat `0x138…`), nicht raten.
6. **Altpfad (FX-Rename) ablösen:** das `renamed_name`-Marking aus
   `reconcileInsertMarkersForTrack_` raus, sobald das Overlay steht. Setting
   bleibt ("Mark active CS/BC in Inserts list"), nur die Mechanik wechselt.

## Offen / Risiken für den Vollausbau

- **Scrolling der fxlist** (lange Chains): Zeile kann aus dem Block scrollen →
  Overlay clippen oder ausblenden.
- **Mixer sichtbar?** Wenn der Mixer zu/Track ausgeblendet ist, kein Rect →
  no-op.
- **Theme-Abhängigkeit** der Zeilenhöhe → Kalibrier-Routine statt Konstante.
- **defer-Companion-Lifecycle:** mit der Extension koppeln (auto-start/stop),
  damit der User nichts manuell starten muss.
