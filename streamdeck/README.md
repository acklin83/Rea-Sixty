# Rea-Sixty Companion — Stream Deck plugin

Fire Rea-Sixty / REAPER actions from an Elgato Stream Deck and mirror live
surface state (selected track, per-track peak & gain-reduction meters) onto the
keys.

## What you need first

This plugin is a remote for **Rea-Sixty**, so you need Rea-Sixty running:

- **REAPER** with **Rea-Sixty** installed, and REAPER open. If you already use
  Rea-Sixty, you're good.
- The **Stream Deck app** (version 6.5 or newer — basically any current one).

You do **not** need to install anything else (no Node, no extra tools).

## Install — 3 steps

1. **Download the plugin** from the Rea-Sixty releases page:
   **<https://github.com/acklin83/Rea-Sixty/releases/latest>** — under
   *Assets*, click the file named **`com.reasixty.companion.streamDeckPlugin`**.
   (The long name is normal for Stream Deck plugins — don't worry about it.)
2. **Double-click the file you downloaded.** The Stream Deck app pops up and
   installs it for you. There's nothing to set up.
3. **Open the Stream Deck app.** On the right, in the list of actions, you'll see
   a **Rea-Sixty** section. Drag any tile onto a key, and set it up in the panel
   that appears.

Done. As long as REAPER is open with Rea-Sixty, the keys just work. If you start
REAPER later, the keys connect by themselves — no need to reinstall.

> **Nothing happens when I press a key?** Make sure REAPER is open and Rea-Sixty
> is loaded. That's almost always it.

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
- **Track name** and **Track colour** (tints the key) are optional.
- **Smart abbrev.** (on by default) shortens long names. It *overrides* the
  global Track-name mode rather than following it — ticked gives you smart
  abbreviation even when the surface is set to plain truncation, unticked shows
  the full name.
- **Name position** (Bottom / Top — top reads better on an angled deck) and
  **Wrap name** (allow a second line).
- **Font size:** Small / Normal / Large / Extra large.
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

> **This plugin cannot follow the port.** It dials `127.0.0.1:49900` and has no
> host or port setting (`plugin.js:173-174`), so moving the bridge disconnects
> the Stream Deck keys for good. Only change the port if the Companion module —
> which does have host and port fields — is your only client.

**Same machine only.** For the same reason, opening the bridge to the LAN
(`sd_bridge_bind = lan`) does nothing for this plugin: it always connects to
localhost. That option is for Companion on another machine.

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
