# companion-module-reasixty

A [Bitfocus Companion](https://bitfocus.io/companion) module for **Rea-Sixty**
(the SSL-360°-style REAPER control-surface extension). It connects to the
Rea-Sixty bridge inside REAPER — the same TCP/NDJSON bridge the Stream Deck
plugin uses — so Companion can fire Rea-Sixty / REAPER actions and mirror live
surface state (selected track, layer, flip, peak & gain-reduction meters).

No Elgato Stream Deck app required.

## Requirements

- **Bitfocus Companion 4.x** (module API v2 — this module targets
  `@companion-module/base` 2.x).
- **REAPER** with the **Rea-Sixty** extension. The bridge listens on
  `127.0.0.1:49900` by default.

## How it fits together

```
Companion  ──TCP :49900 (NDJSON)──▶  Rea-Sixty bridge (StreamDeckBridge.cpp)
   module        actions/meters             inside the REAPER extension
```

The bridge is protocol-agnostic; this module is just a client. See
`../extension/src/StreamDeckBridge.h` for the wire protocol and
`src/bridge.js` for the client.

## Install as a developer module (for testing before it's in the store)

1. Build/prepare this folder with its dependencies:

   ```sh
   cd companion
   npm install
   ```

2. In Companion, set the **Developer modules path** to the folder that
   *contains* this module folder, then restart Companion. (Companion →
   Settings → “Developer modules path”, or the `COMPANION_DEV_MODULES` env var.)

   Point it at the `companion/` directory here; Companion loads any subfolder
   with a `companion/manifest.json`.

3. In Companion, **Connections → Add connection → search “Rea-Sixty”**, set the
   REAPER host/port, and go.

## Package a distributable

```sh
cd companion
npm install
npm run package        # → pkg/ (uses @companion-module/tools build-connection)
```

## Source layout

| File | Purpose |
|------|---------|
| `src/main.js` | Instance class (v2: default export + named `UpgradeScripts`), config, bridge wiring, state → variables/feedbacks, metering set. |
| `src/bridge.js` | `BridgeClient` — TCP/NDJSON client with auto-reconnect. |
| `src/actions.js` | Actions built live from the Rea-Sixty catalogue + REAPER id/action. |
| `src/feedbacks.js` | Layer / flip / meter-over / graphical meter-bar feedbacks. |
| `src/variables.js` | Static + per-metered-target variables. |
| `src/meter.js` | RGBA meter-bar rasteriser + colour ramps (no image deps). |
| `src/presets.js` | Per-category action presets + meter/status presets. |
| `src/upgrades.js` | Config migrations (none yet). |
| `companion/manifest.json` | Module manifest (Node 22, nodejs-ipc, api 2.x). |
| `companion/HELP.md` | In-app help shown in Companion. |

## Notes

- Targets Companion 4.x / module-api v2. Key v2 differences from older modules:
  no `runEntrypoint` (the entry file `export default`s the class and
  re-exports `UpgradeScripts`); `setVariableDefinitions` takes an object;
  `setPresetDefinitions(structure, presets)` takes two args; ESM only.
- To reach REAPER from a different machine, open the bridge to the LAN with the
  REAPER ExtState `rea_sixty/sd_bridge_bind = lan` (see HELP.md).
