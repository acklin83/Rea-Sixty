# Rea-Sixty

Control **Rea-Sixty / REAPER** from Bitfocus Companion, and mirror live surface
state (selected track, binding layer, flip, peak & gain-reduction meters) onto
your buttons.

This module talks to the **Rea-Sixty bridge** built into the REAPER extension —
the same bridge the Stream Deck plugin uses. You do **not** need the Elgato
Stream Deck app.

## What you need

- **REAPER** with the **Rea-Sixty** extension installed and REAPER open.
- This module (Connections → add → search "Rea-Sixty").

That's it. When REAPER is open with Rea-Sixty, the buttons work; if you start
REAPER later, the module reconnects on its own.

## Connection settings

| Field | Meaning |
|-------|---------|
| **REAPER host** | Where REAPER runs. `127.0.0.1` if Companion is on the same machine. |
| **Bridge port** | Default `49900`. Only change it if you changed it in REAPER. |
| **Metering** | Which tracks report live Peak / Gain-Reduction: the selected track, the master, and/or a list of fixed track numbers. Keep it tight — each metered track is polled ~15×/s. |

### Companion on a different machine than REAPER

By default the bridge only accepts connections from the same computer. To let
Companion connect from another machine, run this once in REAPER (Actions →
Show action list → ReaScript console, or any script) and **restart REAPER**:

```lua
reaper.SetExtState("rea_sixty", "sd_bridge_bind", "lan", true)
```

There is no password on the connection, so only do this on a trusted network.

## Actions

- **Rea-Sixty action** — fire any Rea-Sixty built-in. The list is pulled live
  from your installed extension, so it always matches your version. The
  *Parameter* field is `0` for almost everything; a few actions (e.g. selecting
  a layer or bank) use it as an index.
- **REAPER command (by ID)** — run a REAPER command by its numeric ID
  (e.g. `40044` = Play/Stop).
- **REAPER command (by action string)** — run a named REAPER / SWS / custom
  action, e.g. `_SWS_ABOUT`.

## Feedbacks

- **Binding layer is active** — colour a button when layer 1 / 2 / 3 is active.
- **Flip mode is on** — colour a button while Flip is engaged.
- **Meter over threshold** — colour a button when a track's Peak (dBFS) or Gain
  Reduction (dB) crosses a threshold you set.
- **Meter bar (graphical)** — draws a live Peak / Gain-Reduction bar on the
  button (green → amber → red for peak; the GR bar fills from the top).

## Variables

- `connection` — `connected` / `disconnected`
- `sel_track_name`, `sel_track_number` — the selected track
- `active_layer`, `flip`
- Per metered track: `<t>_peak_db`, `<t>_gr_db`, `<t>_bc_gr_db`, `<t>_name`
  where `<t>` is `sel`, `master`, or `trk_<N>` (only for tracks you enabled in
  the Metering settings).

## Presets

Ready-made buttons are provided for **every** Rea-Sixty action (grouped by
category), plus meter buttons for each metered track and status buttons for
connection / selected track / layer / flip.

## Troubleshooting

- **Actions do nothing / meters blank:** make sure REAPER is running with
  Rea-Sixty, and that the host/port match. The connection status (top of the
  Connections list, and the `connection` variable) tells you if the bridge is
  reachable.
- **Action list only shows a placeholder:** the module hasn't connected yet —
  check host/port and that REAPER is open.
- **Meters show nothing:** enable the track in the module's **Metering**
  settings (selected / master / by number).
