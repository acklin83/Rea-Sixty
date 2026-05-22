# Rea-Sixty v0.1.5

Bugfix release. Replaces the v0.1.4 "Hide tracks in collapsed folders" toggle (which only caught one of REAPER's three folder-collapse states and didn't affect the channel encoders at all) with a single TCP / MCP follow radio that defers to REAPER's own visibility model. The surface now skips hidden / collapsed-folder children consistently across the UF8 ChannelEncoder, the UC1 CHANNEL encoder, and every bindings-routed Select-Relative path.

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

### TCP / MCP follow radio (replaces three separate toggles)

`Settings → Device → Tracks` now has a single `Surface mirrors:` radio with two choices: **TCP** (default) or **MCP**. The surface follows REAPER's own per-view visibility — `IsTrackVisible(track, mixer)` folds the per-track `B_SHOWIN*` flag, the global view-hidden flag, AND every ancestor's folder-collapse state into one call. No more guessing at `I_FOLDERCOMPACT` values.

What this replaces:
- Old `Show tracks hidden in TCP` checkbox (default off)
- Old `Show tracks hidden in MCP` checkbox (default off)
- Old `Hide tracks in collapsed folders` checkbox (default off)

In **TCP mode**, collapsed-folder children disappear from the strips and from the channel-encoder scroll, as long as REAPER's *Preferences → Appearance → Track Control Panels → Hide children of collapsed folders* preference is enabled. In **MCP mode**, the surface mirrors what the mixer shows (folders don't collapse in the mixer, so hidden children stay scrollable).

Existing v0.1.4 ExtState carries over: if you had `Hide tracks in collapsed folders` on, you land on TCP-follow; if you only had `Show tracks hidden in MCP` ticked, you land on MCP-follow; everything else defaults to TCP.

### Channel encoders respect the visibility filter

The v0.1.4 folder-collapse filter only hid strips visually — both channel encoders still scrolled through the hidden tracks. v0.1.5 plumbs the filtered visible-track list into every track-step navigation path:

- **UF8 ChannelEncoder** (bindings `ChSelect` mode + Nav-mode SelectRelative)
- **UC1 CHANNEL encoder** (kChannelEncoder handler)
- **PendingInput::SelectRelative** drain branch (now delegates to the same code path)

Bank-jump / bank-by-one were already correct in v0.1.4; this release closes the channel-encoder gap.

### Encoder dispatch action label

`Encoder: dispatch by current mode` — dropped the eight-mode enumeration that was rendering as a wall of text in the action picker.

## Bug fixes

- Channel encoders no longer scroll past hidden / collapsed-folder tracks (the primary user report against v0.1.4).
- TCP-vs-MCP visibility intent is now a single source of truth, so a user can't accidentally end up with inconsistent display vs. scroll behaviour.

## Known issues

Same as v0.1.4. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.5.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.5.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.5.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
