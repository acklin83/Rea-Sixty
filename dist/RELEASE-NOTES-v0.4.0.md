# Rea-Sixty v0.4.0 — "You've Got to Haggle!"

**Rea-Sixty gets a home on the web, and a place to trade mappings.** [reasixty.com](https://reasixty.com) is live — what the extension is, why (versus SSL 360°), the features, how to install, and the full manual. And with it the **Mapping Exchange**: upload your plug-in mappings and install everyone else's, either from inside REAPER or in the browser. Alongside the platform, this release carries a batch of Plug-in Learn refinements.

## Install via ReaPack (recommended)

```
Extensions → ReaPack → Manage repositories → Import/export → Import repositories
```
Paste:
```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```
Then **Browse packages** → `Rea-Sixty` → Install. Restart REAPER. Preferences → Control/OSC/Web → Add → Rea-Sixty.

First-run setup buttons (`Settings → About`):
- **Windows:** "Install UF8/UC1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## What's new

### The website — reasixty.com

A proper home on the web. It is **generated from the repo**, so the manual on the site is the exact same file that ships inside the extension — the two can never drift apart. And it keeps the plug-in's own promise: **no tracking, no external hosts, no third-party embeds** — system fonts, self-hosted assets, nothing else. It also carries an account area: sign in with a **passkey** or a **magic link**, issue a device token for REAPER, and manage the mappings you have published.

### The Mapping Exchange

A bazaar for plug-in mappings. Upload your UC1, UF8 or UC1+UF8 mappings and grab everyone else's.

- **Browse by plug-in** — one row per plug-in, sortable and searchable by vendor, with a **coverage** figure showing how much of a plug-in a mapping drives and a "**works for me**" vote instead of stars.
- **Read a mapping in full** — each one is a control-to-parameter table grouped by section; two mappings can be **compared side by side**, and UF8 maps show the complete per-fader-bank V-Pot grid the way the hardware lays it out.
- **Two ways in.** Browse and install right inside REAPER (`Settings → Exchange`), or from the website. Publishing is one click from the FX-Learn **Share** dialog ("Publish to exchange").
- **Link a machine** to get a device token without copying a secret around — a short code, one confirmation in the browser, and the token arrives in the extension by itself.
- **Everything is CC0.** **Mappings only** — never `.rea60config` setup bundles: those can carry actions and macros, so importing one would run whatever its author put in it, and the exchange refuses them.
- Vendor grouping (with alias folding for UADx / Acustica and the like), duplicate detection so two identical mappings of the same plug-in can't both be published, and a moderation queue.

### Plug-in Learn

- **Fill Sequential carries the scribble label too.** Give Fader 1 the name "Ch 1", Fill Sequential, and the strips fill "Ch 2", "Ch 3" … "Ch 16" — the label steps with the parameter instead of being copied verbatim. Fader and V-Pot labels, in both the Settings editor and the Learn-HUD.
- **The Learn-HUD's UF8 colour bars are clickable, like the FX-Learn mockup.** Left-click a strip's bar to pick its colour, right-click to fill all eight strips of the active bank.
- **FX-Learn follows the modifier.** Tap **Ctrl**, **Opt** or **Ctrl+Opt** to switch the schematic to that overlay's mapping — tap the same combo again to return to Normal — so you no longer have to reach for the Layer radio. (UC1 layers.)
- **Params bound in the UC1 EXT FUNCS menu now show as bound** in the parameter list (greyed, with their EXT label), instead of looking free to grab.

## Known issues

Same as v0.3.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.0.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.0.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.0.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
