# Rea-Sixty v0.6.0, "Six Seasons and a Movie"

**The release where a soft-key set became a thing you own, and where the surface started talking past REAPER.** Nine sets, each with a number and a name, all of them in one matrix you edit in place: double-click renames a bank, right-click copies, cuts, pastes, or turns it into a dynamic bank. Two of the nine were walled off until now, the coordinates SSL's own channel-strip and bus-comp rows sit on; they are in the matrix like the rest, editable wherever the plug-in leaves a key free. Next to that, two things that reach outside: **OBS** on the keys, with recording, chapter marks and scenes by name, and a **Sticky Pot that can carry two parameters at once**, counter-running, which is how you drive an 1176 harder without the level moving. The Learn HUD grew a column that says where every parameter already sits, AutoLearn stopped proposing for the wrong plug-in, and the push-cycle menus that took two seconds to answer now answer at once.

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

## Soft-key sets

**Nine sets, and each one has a number.** Three Quick keys on three layers is nine sets of six banks, and every one of them is now addressable: *Soft-Key Set, engage* takes the set number as its parameter, so a set sits on any key you like, a foot switch, a Stream Deck tile. The Quick key is one way in, not the only one. Sets 8 and 9 are the two Layer 1 coordinates that carry SSL's own channel-strip and bus-comp rows. The editor used to refuse those two outright, although the free keys on them have been dispatchable and paintable for months: where the SSL tables put a parameter the plug-in keeps the key, and everything else on those rows, plus the whole Shift half, is yours. They were added as 8 and 9 rather than renumbering 1 to 7, because bindings point at these numbers.

**The matrix is the editor.** Nine rows, six columns, every set with its banks. Click a cell and you are on that bank, on the surface and in the editor below. Double-click renames it. Right-click gives you copy, cut and paste of the whole bank, its dynamic kind, your saved presets and the factory banks. The set's name is typed in the row, and Plain or Shift is picked in the header, because the matrix shows one of the two at a time.

**A bank can be named, and the name is announced.** The UF8 has no display to write it across the way the UF1 does with its time field, so it flashes in the mode-change banner when you switch bank, and the focused-track panel carries a menu of all of them. The set comes first, always: a bank called *KHE Amps* in a set called *User 1* reads *User 1, KHE Amps*.

**Two more dynamic bank kinds**, and the table in the manual now lists all eight: the eight Hue scene slots, which were reachable before but undocumented there, and the OBS scenes below.

**A factory bank longer than eight keys spills into Shift.** *Encoder Modes* carries all fifteen encoder modes that way, eight on Plain and seven on Shift, one modifier apart. Saving a bank of your own as a preset takes its Shift half with it.

**Every built-in action now brings a twelve-character default label**, and it lands in the Label field the moment you pick the action, until you type one of your own. A build without a label or a description for a new action fails, so the gap cannot reach a release.

## OBS

**Recording, chapter marks and scenes, from the keys.** Rea-Sixty speaks obs-websocket, which has shipped inside OBS since 28. Switch the server on in OBS under *Tools*, then fill in host, port and password under *Settings, Modes, OBS*.

- **OBS: start / stop recording** — the lamp follows what OBS reports, not what the key asked for. Stop the recording in OBS itself and the lamp goes out on its own.
- **OBS: pause / resume recording**, and **OBS: chapter mark in the recording**, which drops a mark so the take is findable in the edit.
- **OBS: switch to scene** — pick it **by name** from a drop-down of what OBS is showing, or type a name if OBS is not running yet. The old number still works. A number counts positions, and positions move when you reorder the scene list in OBS.
- A soft-key bank of kind **OBS Scenes** puts the scene names on eight keys, live, with the one on air lit.
- A marker named `obs: Wide` switches to that scene as the playhead passes it, forwards and only while rolling, the same rule the Hue cues follow.

The password is kept in its own settings value, apart from host and port, so a shared setup carries the connection without the credentials.

## Sticky Pot

**A pin can carry a second parameter.** Some parameters do not work alone: drive an 1176 harder and it compresses more and gets louder with it, so the output has to come down as the input goes up. Fire *Sticky Pot: Pair next touched Parameter*, touch the second parameter on the same track, and it follows the pinned one, counter-running one for one by default. Both values are anchored the moment you pair them, so nothing jumps, and moving either of them by hand afterwards is picked up from there rather than undone by the next detent. The ratio is yours to change.

**A pane of its own** lists every pin in the project with its track, plug-in, parameter and live value, the ratio of a pair, and buttons to unlink or clear. A pin whose track or plug-in is gone still shows, and can still be cleared.

**Fixed while building it:** letting go of the fader under FLIP wrote the *focused* parameter, or pan, instead of the pinned one. The release path was meant to mirror the live one and had every rung but that one. It has been wrong since FLIP learned to carry a pin in August; a lone pin hid it, a pair made it plain.

## The Learn HUD, FX Learn and AutoLearn

**The parameter list says where a parameter already is.** A column on the right names the slot on the UC1, the bank and strip on the UF8, and the EXT FUNCS cells, so you can see what is taken before you take it again.

**The UC1's EXT FUNCS strip is visible and assignable in the HUD**, with rename and clear.

**AutoLearn moved into the HUD's parameter drawer** and got the fixes the day at the desk turned up: it proposes for the plug-in the HUD is showing rather than for a mapped one elsewhere, for the tab you are on and the right surface, it recognises a channel matrix and maps it as one, it maps a plug-in that has no map at all, numbered EQ bands map by their number, *Knee* is no longer confidently matched to *Mix*, and applying to a factory map is refused the way every other write to one is. It also lists what it did **not** match, and stays on the plug-in you just mapped instead of jumping to the last one.

**UF8 Plug-in Mode ships on Shift + PLUGIN**, and Esc really cancels an armed learn.

## UF1

**Both footswitches are bindable** like the UF8's, with the ids measured rather than guessed.

**The preset browser owns its screen**, and it reads whichever library the plug-in has: SSL's own on-disk library for an SSL plug-in, which exposes nothing to REAPER's list, and REAPER's own for everything else. `Shift` on the `5-8` key opens it in the Plugin view, where that key is otherwise idle.

**Four keys carry a binding per view**, so the same key can mean different things in Plugin, DAW, Meter and Sends.

**Fades mode can show REAPER's crossfade editor.** Off by default; with it on, the window comes up while the edge you are aiming at is a crossfade and goes away again on a plain fade. A window you had open yourself is never closed.

## Speed

The push-cycle menus and the FX-Learn parameter lists were rebuilding themselves on every timer tick and every frame; a Pro-Q with hundreds of parameters made that visible as a two-second wait after every click. The catalogue is built once per plug-in, the proposals once per answer, the parameter names are indexed rather than scanned, the catalogue is written once the edits stop rather than on every keystroke, and a parameter that reports a step size is no longer treated as an option list unless it really is one (the cap is 64 entries).

## Also

- **Windows:** the combined FX-Learn layer is **left Alt with left Ctrl**, which is what the left hand can actually hold.
- **Hue:** the recording light can recall a scene instead of a colour, and the bridge is not read when it does not have to be.
- **The manual was audited against the code**, chapter by chapter, and around fifty claims that had drifted were corrected — the channel encoder's fifteen modes, the bus comp's six soft-key pages, the number of tabs, and a long tail of smaller ones.

## Configuration

The bindings file moves to **version 34**. Both bumps are additive: v33 gives a UF8 sub-bank its own name, v34 gives a set a name and a number. A configuration from v0.5.9 loads unchanged, and an unnamed set reads as *Set N*, which is what every set was before. Projects are untouched; Sticky Pot pairs are stored in the project alongside the pins that carry them.

## Known issues

- Sticky Pot pairs, the crossfade editor and the whole OBS link are new in this release and were built in one week. Check them against your own rig before a session that depends on them.
- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.0.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.0.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.0.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
