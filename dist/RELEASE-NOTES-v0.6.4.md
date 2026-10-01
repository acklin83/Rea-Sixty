# Rea-Sixty v0.6.4, "Fletcher Dragge is my Spirit Animal"

**Spill on the surface.** A VCA lead spills its followers onto the UF8 and the UF1 with a long press on `SEL`, level by level for nested VCAs, and VCA Mode shows only the top leads. Long-press `SEL` became a binding of its own. A new dynamic bank puts the eight Selection Sets on the keys, in their own colours, to recall, store, select and clear. Folders collapsed in the Mixer now take their children off the surface, and Folder Mode keeps its open folders in the project.

## Install via ReaPack (recommended)

Under Extensions, ReaPack, Manage repositories, Import/export, Import repositories. Paste:

```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```

Then Browse packages, `Rea-Sixty`, Install. Restart REAPER. Add the surface under Preferences, Control/OSC/Web, Add, Rea-Sixty.

First-run setup buttons live under Settings, About:

- **Windows:** "Install UF8/UC1/UF1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## VCA spill and VCA Mode

**Long-press `SEL` on a VCA lead in VCA Mode and its followers take the surface.** The lead sits on strip 1 and stays there, its followers follow in track order and bank as usual. While the spill is on, nothing else is on the surface: Folder Mode, Selection Sets, Show Only Selected and the AUTO filter rest until it ends.

- A follower that is itself a VCA lead goes one level deeper on long-press `SEL`; the leads line up on the left, outermost first. Long-press the deepest lead to go back one level, a lead in between to jump to its level, the outermost to leave.
- Leaving returns to the bank you were on. Switching project tabs, or the lead disappearing, ends the spill too.
- The value line reads **VCA Lead** on leads, with **Spill** beside it on the spilled ones. Turning the V-Pot shows the real value for three seconds, as on a Folder Mode parent.
- **VCA Mode** shows only the leads that follow no other VCA.
- *Settings, Behaviour, Tracks, VCA spill shows hidden tracks* (on by default): followers hidden in the TCP or the Mixer come along.
- New actions: **Toggle VCA Mode (top leads only)**, **VCA Spill (selected track)** (in any mode, for the UF1 and keyboards), **Leave VCA Spill**. The two spill actions light while a spill is on.

## Long-press SEL is a binding

**Settings, Bindings, `SEL` has a LONG PRESS column now, on the UF8 and the UF1.** Its factory action is **Spill (folder / VCA)**: in Folder Mode it opens or closes a folder, in VCA Mode it spills a VCA lead. On a UF8 `SEL` it acts on that strip's track, from any other key on the selected track. Bindings files from before get the default filled in; nothing changes until you rebind it.

- The double press moved into the SHORT column, next to the description of the built-in select.
- On the UF1 in REC and REC + MON, long-press `SEL` does nothing, as on the UF8. The double press still fires.

## Selection Sets on a bank

**A soft-key bank of the kind Selection Sets puts slots 1 to 8 on the keys**, on the UF8's eight top keys and on the UF1's four (two pages). Names on the keys, each slot in its own colour (set under Settings, Selection Sets, like the Parameter Groups' colours), the recalled set lit, used slots dim. Push recalls, hold stores the current selection into an empty slot, Shift stores over, Cmd selects the set's tracks in REAPER, Ctrl clears. The banner and the UF1's time field say what was stored or cleared.

## Folders

- **Surface mirrors: MCP follows a folder collapsed in the Mixer.** Its children leave the surface. The Mixer shows the folder button after a right-click on an empty spot and *Clickable icon for folder tracks to show/hide children*. REAPER itself reports those children as visible, which is why the surface kept them before.
- In TCP mode, children of a folder collapsed to *hidden* leave the surface as before; children collapsed to *small* stay, because the TCP still draws them.
- **Folder Mode keeps its open folders.** Switching Folder Mode off and on again shows them as they were, and they are saved with the project, one set per project tab.

## Groups

- Selection Set group slots reach all 128 track groups of REAPER 7.23 and later (64 before).
- The UF1's razor area spreads across media-edit groups in all 128 groups too.

## REAPER actions

- *Rea-Sixty: Folder Mode (parents only) (toggle)*, *Rea-Sixty: VCA Mode (top leads only) (toggle)*, *Rea-Sixty: VCA spill (selected track)*, *Rea-Sixty: Leave VCA spill*. A toolbar button or key bound to one of them lights like the Rea-Sixty action of the same name.

## Fixes

- The mode banner names the UF1's RME side-car. With the UF1 set to start in RME it said "UF1 Mode • DAW" at REAPER start, and entering or leaving the side-car with SHIFT + MODE showed nothing.
- Settings text named a REAPER preference that does not exist ("Hide children of collapsed folders"). It now names the real one, *Folder collapse button cycles track heights*.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.4.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.4.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.4.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
