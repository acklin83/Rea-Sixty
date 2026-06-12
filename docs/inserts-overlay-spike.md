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

## Bitte beim Testen notieren

- REAPER-Version + Capability-Report (Console-Kopf).
- Für MCP **und** TCP je eine Zeile: `win` / `seg` / `det` und ob das Rect
  pro Insert wandert.
- Sitzt das Highlight deckungsgleich auf der Zeile? Bleibt es beim Scrollen /
  Mixer-Resize korrekt? Wird es beim Verlassen sauber entfernt?
