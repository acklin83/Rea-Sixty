# Rea-Sixty v0.3.3 — "Almost, but not quite, entirely unlike tea."

**The manual has caught up.** It still described v0.2, so four releases' worth of features — Send and Receive views, dynamic soft-key sub-banks, DynaMount, Stream Deck and Companion — were shipped but undocumented, and roughly twenty of its existing statements had quietly gone false. It is now current with the shipped build, ~430 lines longer, and every claim was verified against the source rather than read off a changelog. The manual lives in `Settings → Manual` as well as on GitHub, so this arrives in the app.

Alongside it: the MCP Inserts overlay highlight, which sat on the wrong row in any mixer that was not a plain FX list. That took five passes because each cause hid the next.

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

### The manual is current again

Four chapters that never existed, plus a rewrite:

- **Sends and Receives.** The manual had three passing mentions of routing and explained none of the behaviour. Both layouts are now covered — "sends of the focused track" across the strips, and "Send N of every track" down the bank — including the part that surprises people: in the focused-track layouts the **BANK ←/→ keys page the send list**, not the strips. Paging exists only there.
- **Dynamic soft-key sub-banks** — FX, Parameter Groups and Track Colours. Note that the FX keys have **three** LED states: bright is the focused plug-in, dim is any other running plug-in, dark is bypassed, offline or empty. Paging is off by default.
- **Hardware Modes**, surface mirroring, and the Restart action.
- **DynaMount mode**, **Stream Deck** and **Bitfocus Companion**.
- **Favourites, rewritten** for the current CS + BC model. Four consecutive releases rebuilt that system; the old chapter's title no longer described it.

And about twenty corrections to what was already there, each checked against the code:

- The sidebar has **ten** tabs, not eight — Favourites and Manual were missing. DynaMount is a sub-tab under **Modes**, not a tab of its own.
- **Log paths are not `/tmp` on Windows.** The old text sent Windows users to a folder they do not have.
- **macOS Intel has been a shipped binary since v0.1.30**, not build-from-source only.
- "Per-binding-file export does not exist" had been false since v0.2.5 — *Save / Load UC1 bindings…* are there.
- The tested-REAPER range, the version strings and all three download filenames still said v0.2.
- The **Console output** checkbox (off by default) is now documented in both the Logs section and Diagnostics — without it, someone following the troubleshooting chapter sees a silent Console and concludes logging is broken.

### Repo documentation corrected

Three sources contradicted the code and would each have put something false in front of someone:

- **DynaMount axes were inverted** in `docs/dynamount-mode.md` — the fader is **X**, FLIP is **Y**. The in-app Settings footer always said so correctly; only the doc was wrong. V-Pot rotation is also not sent live per detent — it is debounced a second after the last one.
- **`streamdeck/README.md`** explained how to move the bridge port without mentioning that the Stream Deck plugin cannot follow it — it hard-codes `127.0.0.1:49900`. Following that section as written silently killed the keys. Same reason the LAN bind does nothing for Stream Deck, which is now stated. The meter-options list was also four releases out of date.
- The **v0.3.1 release notes** claimed LAN binding lets the Stream Deck plugin reach REAPER from another machine. It does not — that is Companion only. Corrected with a note above the original text rather than a rewrite; the published release still carries what shipped.

## Bug fixes

- **The MCP Inserts overlay highlight now lands on the right insert.** It was computed from a row number, which only holds when the visual row happens to equal the plug-in's position in the chain — false as soon as an empty slot or a grouped FX parameter shares the list. The overlay now **reads** the mixer's own row labels to find the insert instead of counting rows, so empty slots and expanded parameters no longer push the highlight onto the wrong thing.
- Toggling **"show FX parameters"** re-lays out the list in place, which previously triggered no re-measure and hit a cache that was only cleared on mixer scroll — so the highlight stayed wrong until you changed track. Both are fixed; it now follows the toggle live.
- The highlight's height comes from the measured row, and a 1-px offset the old arithmetic always carried is gone. `overlay_rowh` / `overlay_toppad` no longer need tuning to be correct.

Verified on hardware across empty FX slots, ungrouped parameters and grouped parameters, including switching between them live.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.3.3.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.3.3.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.3.3.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
