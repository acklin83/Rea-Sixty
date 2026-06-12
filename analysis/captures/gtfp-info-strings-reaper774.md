# GetThingFromPoint — Info-String-Landkarte (REAPER 7.74, macOS)

Capture vom 2026-06-12 mit `analysis/spike_inserts_overlay.lua`. Maus über
TCP (Arrange-Trackpanel), MCP (Mixer-Strip) und Master gefahren. Jede Zeile =
ein erkanntes UI-Element: `win` (HWND-Adresse), `info` (Element-Kennung),
`rect` (Screen-Koords, vom Reverse-Scan ermittelt), `isFx` (info enthält "fx").

**Kernerkenntnis:** `GetThingFromPoint` liefert **region-genaue** Kennungen,
NICHT zeilengenau. Die Insert-Liste kommt als **ein Block** `mcp.fxlist` /
`tcp.fxlist` ohne FX-Index zurück. Per-FX-Zeile muss selbst gerechnet werden.

## Zwei Mixer-Fenster

- `win=0x108008000` — Haupt-Hauptfenster (Arrange-TCP **und** angedockter Mixer).
- `win=0x138028000` — separater/zweiter Mixer (hier die Master-Strips + ein
  zweiter Track-Mixer rechts).
- `win=` (leer) — `trans` / `trans.automode`: Transport, kein HWND-Treffer.

→ Overlay muss das richtige HWND treffen (via `JS_Window_FromPoint` am
Zielpunkt, nicht das gfx-Spike-Fenster).

## Info-String-Vokabular

### Insert-Liste (Ziel des Features)
| info          | rect (Beispiel)              | isFx | Bemerkung |
|---------------|------------------------------|------|-----------|
| `mcp.fxlist`  | x=0 y=749 w=84 h=228 (108…)  | true | **ganzer Block**, kein Zeilenindex |
| `mcp.fxlist`  | x=1795 y=659 w=123 h=318 (138…) | true | Master-Strip-FX-Liste |
| `mcp.sendlist`| x=0 y=520 w=84 h=225         | false| Send-Liste, analog (auch ein Block) |
| `mcp.fxin`    | x=6 y=409 w=75 h=12          | true | "FX input" Knopf, nicht die Liste |

### TCP (Arrange-Trackpanel) — keine FX-Namensliste sichtbar, nur:
`tcp.trackidx`, `tcp.label`, `tcp.recarm`, `tcp.volume`, `tcp.pan`, `tcp.env`,
`tcp.fx` (FX-Knopf, ~20px), `tcp.fxbyp` (Bypass), `tcp.fxparm` (eingebettete
Param-Zeile, **~21px hoch** — guter Zeilenhöhen-Referenzwert), `tcp.io`,
`tcp.meter`, `tcp.mute`, `tcp.solo`.

### MCP (Mixer-Strip)
`mcp.trackidx`, `mcp.label`, `mcp.env`, `mcp.phase`, `mcp.volume`,
`mcp.volume.label`, `mcp.meter`, `mcp.io`, `mcp.solo`, `mcp.mute`, `mcp.recarm`,
`mcp.recmon`, `mcp.recmode`, `mcp.recinput`, `mcp.fxin`, `mcp.pan.label`,
`mcp.sendlist`, `mcp.fxlist`.

### Master
`master.mcp.label`, `master.mcp.mono`, `master.mcp.menubutton`,
`master.tcp.mono`.

### FX-Plugin-Fenster (NICHT die Liste — offenes Plugin)
`fx_1` mit großen Rects (z.B. 419×311) — das ist das geöffnete FX-Fenster,
verwechselbar mit der Liste, aber `isFx=true` + großes Rect = offenes Plugin.

## Konsequenz für den Vollausbau

1. `GetThingFromPoint` taugt zum **Auffinden des fxlist-Blocks**, nicht der
   Zeile. → Zeilenhöhe aus Layout rechnen.
2. Strip-Rect jedes Tracks mausfrei via `I_MCPX/Y/W/H` bzw. `I_TCPY/H`
   (`GetMediaTrackInfo_Value`). Den fxlist-Offset innerhalb des Strips einmalig
   per Scan kalibrieren (Theme-konstant).
3. Zeile N = fxlist-Top + N · Zeilenhöhe. Zeilenhöhe ≈ `tcp.fxparm`-Höhe (21px
   bei diesem Theme) — pro Theme/Zoom kalibrieren, nicht hartkodieren.
</content>
</invoke>
