# Companion → Stream Deck migrator (Rea-Sixty)

Converts your **Bitfocus Companion** buttons into a native **Elgato Stream Deck**
profile that drives Rea-Sixty. One self-contained HTML file — no install, no
Python, works offline. Nothing is uploaded; all conversion happens in your browser.

## Why it works
Companion and the native Stream Deck plugin are two clients of the **same**
Rea-Sixty bridge (`127.0.0.1:49900`). A button fires the identical command on
either side, so migrating is just re-packing the button config into the Stream
Deck profile format.

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

## What migrates
- **Rea-Sixty action** buttons (builtin + parameter)
- **REAPER command** buttons (by ID and by action string)
- **Meter** buttons (the `meter_bar` / `meter_over` feedback → native meter tile)

Companion **pages become folders** on the Stream Deck; pages that hold more
buttons than fit auto-paginate with `▶ More` / `◀ Back` keys.

## Limits (shown in the preview)
- A Stream Deck key fires **one** command. If a Companion button chained several
  actions, the **first** Rea-Sixty action is kept and the rest are flagged.
- Buttons that used non-Rea-Sixty actions (internal Companion actions, other
  modules) are **skipped** and listed under "controls skipped".
- The button **label** carries over; custom colours/PNG icons do not.

The device is intentionally left unset in the profile so it imports cleanly on
any Stream Deck — you assign it during import.
