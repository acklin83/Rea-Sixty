# Companion → Stream Deck migrator (Rea-Sixty)

Converts your **Bitfocus Companion** REAPER buttons into a native **Elgato Stream
Deck** profile that drives REAPER through the Rea-Sixty plugin. One self-contained
HTML file — no install, no Python, works offline. Nothing is uploaded; all
conversion happens in your browser.

## What it reads
- The official **REAPER** Companion module (`cockos-reaper`) — the one most REAPER
  users have.
- Rea-Sixty's own Companion actions (if you use those too).

## Why it works
The Rea-Sixty Stream Deck plugin fires REAPER commands through the extension's
bridge (`127.0.0.1:49900`). Any Companion button that fires a REAPER command
(by ID or action string) has a direct equivalent, so migrating is re-packing the
button into the Stream Deck profile format.

## Steps (Windows)
1. Install the **Rea-Sixty Companion** Stream Deck plugin
   (`com.reasixty.companion.sdPlugin`) into the Elgato Stream Deck app.
2. In Companion → **Import / Export → Export → Full Configuration** → save the
   `.companionconfig` file.
3. Double-click **`companion-to-streamdeck.html`** to open it in any browser.
4. Drop the `.companionconfig` in, pick your Stream Deck model, review the
   preview, click **Generate**.
5. Double-click the downloaded `.streamDeckProfile` — the Stream Deck app imports
   it. On import, assign it to your device.
6. Run REAPER with the Rea-Sixty extension. Done.

## What migrates cleanly
- **Custom Action** (`/action <command ID>`) — the REAPER module's raw-command
  button. Maps 1:1 to a REAPER command ID / action string. This is the main case.
- **Transport**: Play, Stop, Record, Pause, Toggle Repeat, Toggle Metronome,
  Unsolo-all → their native REAPER command IDs.
- **Rea-Sixty** actions (builtin + parameter, REAPER command, meter feedback).

Companion **pages become folders** on the Stream Deck; pages that hold more
buttons than fit auto-paginate with `▶ More` / `◀ Back` keys.

## What can't auto-migrate (listed in the preview, so nothing is silent)
A Stream Deck key is a single press, so these are **flagged for manual remapping**
instead of producing a broken key:
- **Track-number** actions (Mute/Solo/Arm/Select track *N*) — REAPER key commands
  act on the *selected* track, not a fixed number.
- **Volume / pan faders** — a key can't set a continuous value.
- **Go to marker / region**, momentary **rewind / fast-forward**, raw **OSC
  messages**, and Custom Actions that use a **Companion variable** (`$(...)`).

Also: a Stream Deck key fires **one** command — if a button chained several
actions, the first migratable one is kept and the rest are noted. The button
**label** carries over; custom colours / PNG icons do not.

The device is intentionally left unset in the profile so it imports cleanly on
any Stream Deck — you assign it during import.
