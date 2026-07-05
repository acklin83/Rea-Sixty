# Rea-Sixty Companion — Stream Deck plugin

Fire Rea-Sixty / REAPER actions from an Elgato Stream Deck and mirror live
surface state (selected track, per-track peak & gain-reduction meters) onto the
keys.

## Requirements

- **REAPER** with the **Rea-Sixty extension** installed and running. The plugin
  talks to a localhost bridge the extension opens on `127.0.0.1:49900` — without
  the extension (and a running REAPER) the plugin does nothing.
- **Stream Deck app 6.5 or newer.** The Node runtime is provided by the app; you
  do **not** need to install Node yourself.

## Install (users)

1. Install the Rea-Sixty extension in REAPER (via ReaPack) and start REAPER.
2. Download **`com.reasixty.companion.streamDeckPlugin`** from the release.
3. **Double-click** the file. The Stream Deck app installs it automatically.
4. In the Stream Deck app you'll find a **“Rea-Sixty”** category in the actions
   list on the right. Drag a tile onto a key and configure it in the panel.

That's it. As long as REAPER is running with the extension, keys work
immediately (the plugin reconnects on its own if REAPER is started later).

## The tiles

| Tile | What it does |
|------|--------------|
| **Favourite** | Switch / cycle CS · BC · Fav favourites |
| **Layer** | Select binding layer 1 / 2 / 3 |
| **Hardware Mode** | Flip, Home, Settings window, mirrors, overlays, learn … |
| **Navigation** | Bank / Page navigation and Zoom |
| **Plug-in** | FX GUI toggles, chain, close-all, Quick-Learn, bypass/offline |
| **Selection Set** | Recall selection sets / temp selection sets |
| **Surface Target** | Selection / Encoder modes, Sends, Param Groups, Soft-Key banks, SSL |
| **Any Rea-Sixty Action** | Any built-in (grouped), or a raw REAPER command |
| **Meter** | Live Peak / Gain-Reduction readout for a track |

Each tile's action list is pulled **live from the extension**, so it always
matches your installed version. Keys show no icon and no title by default (set a
title in the panel if you want one).

### Meter tile

- **Source:** Peak, Gain Reduction, Bus-Comp GR, or a combined Peak + GR view.
- **Track:** the selected track, the master, or a fixed track by number / name.
- **Show track name** and **Track colour** (tints the key) are optional.
- **On press** can fire any Rea-Sixty action — so a meter key can double as a
  button.

Peak follows REAPER's per-track meter (Pre/Post-Fader as configured). Gain
Reduction reads the first FX on the track reporting `GainReduction` (any
compressor / limiter); "Bus Comp" targets the mapped SSL Bus Compressor.

## Change the bridge port (optional)

The bridge listens on `49900`. To move it (port clash), set a REAPER ExtState
and restart REAPER — in the ReaScript console or a script:

```lua
reaper.SetExtState("rea_sixty", "sd_bridge_port", "49901", true)
```

## Build / package from source (developers)

The repo keeps the plugin source under `streamdeck/com.reasixty.companion.sdPlugin/`
(the bundled `ws` dependency and the packed artifact are gitignored).

```sh
cd streamdeck/com.reasixty.companion.sdPlugin
npm install                     # reproduces node_modules/ws
cd ..
npx @elgato/cli@latest validate com.reasixty.companion.sdPlugin
npx @elgato/cli@latest pack     com.reasixty.companion.sdPlugin --output dist --force
# → dist/com.reasixty.companion.streamDeckPlugin
```

Node 20 is the manifest-mandated runtime (`Nodejs.Version: "20"`); the Elgato
side uses the bundled `ws` package, the bridge side uses raw TCP.

## Troubleshooting

- **Keys do nothing / meter is blank:** make sure REAPER is running with the
  Rea-Sixty extension. Check the plugin log at
  `~/Library/Application Support/com.elgato.StreamDeck/Plugins/com.reasixty.companion.sdPlugin/logs/plugin.log`
  — you should see `bridge connected` and `bridge builtins …`.
- **Action list is empty:** the extension isn't reachable (REAPER closed, or a
  port clash — see above).
