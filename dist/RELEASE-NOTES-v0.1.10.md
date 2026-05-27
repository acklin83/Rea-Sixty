# Rea-Sixty v0.1.10 — *Prettyyyyy... Prettyyyyy... Pretty Good!*

Workflow release. Folder Mode learns about nested folders, a new Temporary Selection Set lives alongside the 8 named slots, UC1 Encoder 1 (CHANNEL) becomes fully bindable like Encoder 2, and per-project persistence is rebuilt on the official SDK hook so the things you save into a project actually come back when you reopen it.

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

### Folder Mode — ancestor-chain spill for nested folders

Long-pressing `SEL` on a folder used to spill only that one folder. Drilling past two levels rolled the spill state forward, so re-pressing the parent would lose the work. v0.1.10 keeps the **full ancestor chain** in the spill set, and a collapse keeps its descendants' spill state in memory (dormant) so re-spilling the ancestor instantly restores the previous drill-down. Toggling Folder Mode off (or re-pressing `folder_mode`) clears the set entirely.

In practice: spill a Level-1 → Level-2 → Level-3 folder once. Collapse the top. Re-spill the top — your Level-3 view is right there, no re-pressing.

### Temporary Selection Set

A ninth, ad-hoc selection set parallel to the 8 named slots. No Settings UI, no slot number — just four bindable built-ins:

- `temp_selset_add` — push REAPER's currently-selected tracks into the temp set.
- `temp_selset_remove` — yank them back out.
- `temp_selset_recall` — toggle the surface filter on / off. Mutually exclusive with the 1..8 slot recall — activating either kind drops the other.
- `temp_selset_scroll` — encoder rotation that walks REAPER selection through the temp set in project order; works whether or not the filter is currently recalled.

Useful for spinning up a working set for a session ("just these 6 drum mics + 2 talkback mics") without burning one of the named slots. Persists per-project.

### UC1 Encoder 1 — fully bindable

The CHANNEL encoder on UC1 has always been hardcoded to "scroll tracks". It's now a normal binding target (`ButtonId::Uc1Encoder1`), with the schematic in Settings → Bindings → UC1 sprouting an ENCODER 1 / ROTATE tile next to Encoder 2's. Default Plain stays `track_scroll` (the same scroll + select + UC1 focused-track follow behaviour as before, just promoted to a built-in); Shift = `instance_cycle` for symmetry with Encoder 2. Cmd / Ctrl free.

### `bc_track_scroll_select` — BC encoder optionally drives the whole surface

Twin of the existing `bc_track_scroll`. The original only moves the UC1 BC carousel — REAPER selection and the UF8 bank stay put. The new variant additionally pulls REAPER selection + UF8 bank to the new BC anchor. Default Encoder 2 binding is unchanged; bind this where you want the BC encoder to "take you" to the track, not just preview it.

### Release codenames

Every release from now on carries a codename, shown on the About tab below the version line. Functionally meaningless — just makes releases easier to talk about. v0.1.10's is *Prettyyyyy... Prettyyyyy... Pretty Good!*

## Bug fixes

- **FX Learn fader label** now appears on the UF8 scribble's upper line (the track-name slot). Previously the user-typed label only surfaced in the value line, and only when the V-Pot bank slot was empty — so for the common case of a mapped V-Pot + a mapped fader, the fader's label was effectively invisible. The label is now consulted on the upper line first, with the plug-in's own short name as fallback.
- **Per-project persistence** rebuilt. The Selection Set slots (1..8) and the new Temporary Selection Set previously used `SetProjExtState` — diagnostics showed REAPER silently dropped those writes on save in some builds (the in-memory state read back correctly, but the .rpp didn't contain the data on next open). Both are now serialised via REAPER's official `projectconfig` SDK hook, writing lines directly into the project's RPP chunk. Global-scoped Selection Set slots (the ones with the **Global** checkbox on) continue using `SetExtState`, which is reliable.

## Known issues

Same as v0.1.9. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.10.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.10.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.10.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
