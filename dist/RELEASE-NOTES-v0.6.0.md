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

## The UF1 as an instrument

**The jog wheel has modes, and each one edits something.** In *Items* the wheel walks the item selection and the nav cross extends it, along the edge it is travelling toward, with your own position taken up again when the axis changes. Item grouping is honoured, and REAPER picks the group rather than the extension working it out, which also covers edit groups, where the group id stays 0. In *Razor* the wheel draws and moves razor areas, Ctrl takes the rectangle lane by lane through the envelope lanes, and an area spreads over the track's media-edit group while REAPER's grouping switch is on. In *Envelope* a channel change takes the envelope lane with it, and Shift on the centre key inserts a point. In *Playhead* a jog step is REAPER's own grid unit, a quarter by default. Items and Razor jogs write undo points, so a wrong turn is one Cmd-Z away.

**Nav Mode was rebuilt around the question it answers.** The pane is one row per question and one column per surface, each surface has its own display switch and its own set of actions, and the UF1 is in it: Nav lands on its soft-keys and its channel encoder, with Regions and Markers as two lists of its own, Bank pages the marker list, and the time field says where a jump went. *Markers in region* is a setting now.

**The UF1's soft-key banks got the UF8's editor.** Ten banks in a matrix, the cell is the editor, rename in the header with the panel showing the name as it is typed, right-click for copy, cut, paste and clear, bank presets, and a pinned startup bank. Encoder and Jog mode are drop-downs, and `uf1_bank_select` reaches the UF8 as well.

**The screens follow SSL 360 2.1.12.** The Analogue meter's entry is byte-for-byte SSL's, the init carries the zone 2.1.12 added, the encoder and jog pickers draw SSL's own list, the VU needle falls through the plug-in's 0.5 s hold instead of dropping between messages, the overload flash holds 100 ms, and the goniometer trail fades as light rather than as the raw number.

## The surfaces on their own, and when they sleep

**Each surface is fully usable without the others.** Fourteen places had a feature standing on a device that was not plugged in: the Extender layout, the Nav overlay, the pinned startup bank, the Learn-HUD's UF8 tab, the colour bar naming a cycled plug-in, the UF8's gain-reduction rows. All of them stand on their own now.

**Sleep.** Under Settings, Devices, Brightness: the surfaces go to zero brightness after a set idle time, 1 to 99 minutes, which is SSL 360's own range. Off by default. Playing or recording counts as activity, any key, fader or knob wakes them, and that first touch only wakes. *Sleep now* is a built-in action for a key.

**The UF1 joined the brightness sliders** and has a column of its own for its displays, after 0x47 turned out to be a panel master and the LCD slider had been dimming the LEDs with it.

**Stability.** A plug-in bypassed, taken offline or stepped through presets from the USB thread, an empty Focus Set pin with the Extender on, a paint on the UF1 and a project load that left the surface blank: four ways to lose REAPER or the picture, all closed.

## Plug-ins: a third factory strip, FLIP and Touch to Learn

**The Harrison 32Classic Channel Strip v2 is a factory strip**, on the UF8, the UC1 and the UF1, with its gate gain reduction, its own presets in the preset browser, its A/B, its PRE key and its EXT FUNCS list as SSL sends it. The 4K strips got their soft-key pages as SSL 360 2.1.12 lays them out, including the colour-dependent LF/HF key on the 4K G and a tenth UF1 page, EXPANDER.

**FLIP and Strip Mode couple both ways**, and FLIP with PAN reaches the fader in Strip Mode. FLIP never puts a binary parameter on the fader, the fader stops commandeering the V-Pots, and under FLIP the UF1's two rows show two different things. A fader reaches 100 % of a parameter rather than 99.9, touching one no longer changes the channel, and a move no longer takes the focused parameter with it.

**The number above the UF1 fader names what the fader moves**, in the unit the plug-in gave it, and a frequency reads the same on every surface.

**Touch to Learn puts the UF8 into Plug-in Mode** by itself, because mapping a plug-in while the surface shows the project is not possible. A fader arms its cell while the mode is armed instead of driving its parameter, and the PLUG-IN key is the way back out.

**Pan in SSL Strip Mode belongs to the strip.** The V-Pot shows the pan it writes, Force Pan drives the strip's own pan, the UF1's pan knob sits on the strip's pan with or without the Extender, and panning no longer takes the focused parameter.

## Layers, banks and keys

**Layers switch Quicks only.** A setting under Bindings separates which bindings the surface carries from which Quicks and banks are live. Layer 3 is a key in the schema again with a working LED, and Layers 2 and 3 come up with a Quick engaged.

**Seven printed keys that did nothing now do what they say:** NORM, REC, AUTO, NAV, NUDGE, FOCUS and the channel-encoder push ship bound. An existing configuration is left as it is.

**CS and BC favourites are dynamic bank kinds**, which retired the two factory banks that did the same by hand and gave back two of the six places in a set.

**The UF8 encoder modes are REAPER actions**, fifteen of them, one per mode, so a mode reaches a keyboard shortcut, a foot switch or a Stream Deck tile. The encoder no longer swallows the first click after a direction reversal.

**A soft-key slot draws its whole step chain.** Add a step, set its type and its wait, and a Note On, a pause and a Note Off sit on one press. The dispatch has run chains for months; the editor showed only the first step.

**A factory bank longer than eight keys spills into Shift**, which is how Encoder Modes fits.

## Fine work

**Helper text became hover boxes** under Devices, Appearance, Behaviour and 26 more panes, so the panes are shorter and the explanation is where the control is. Sel and Encoder mode are drop-downs in the focused-track panel, at a fixed width.

**Text widths were measured at the device:** eight characters on the UF8, eight on the UF1, twelve on the UC1. Names had been abbreviated to widths the displays never had, and a long parameter name bled into the UF8's yellow value zone.

**The MCP inserts overlay keeps up with the chain:** adding a plug-in no longer leaves it short, a move into an empty slot redraws, the frame follows an FX that slides into a free slot, and the overlay redraws when the highlighted row moves.

**Parameter Groups wear a colour**, one per group, on the bank's keys.

**Favourites housekeeping:** a clean-up button for favourites whose plug-in lost its mapping, un-learning no longer leaves a favourite that clones itself, switching a favourite moves the FX-Learn editor and the Learn HUD with it, and the carousel reads the short name.

**Panel LEDs say what the key does.** A key cleared to *Do nothing* no longer sits bright in its active colour, the PLUGIN lamp follows its binding, and the Auto row reads the layer the surface fires.

**Modifier modes and automation keys:** the UF1's Solo and Cut honour the keyboard modifier modes, the Extender knows all four route modes, and the automation-mode keys set every selected track.

**Smaller, still visible:** ReaEQ's band width converts between octaves and Q, the parameter pickers hide REAPER's MIDI-learn entries, the bindings page and the Learn HUD agree on what learning and unbinding mean, a renamed parameter reads the same on all three panels, a UF8 relearn resets invert, an Exchange map can no longer be shadowed by the one it replaces, and Hue labels take eight characters.

## Sends, receives and the fixes after them

The week after the features came a round on the hardware, and the send and receive fader modes took the biggest share of it.

**A send strip is the route's track, everywhere.** In a Send or Receive fader mode the level and gain-reduction rows read the track the route goes to, not the strip's own; the channel number is that track's number; and the UF1 Extender paints its colours the way the UF8 does. SEL follows the same rule: on the Extender's ninth strip it selects the send's track and its lamp says so, and on a send strip where there is nothing to select it does nothing and stays dark.

**FX Learn and the Learn HUD.** Switching a map to "UF1 only" fills the UF1 layer first instead of leaving it empty. A learned channel strip puts its soft-keys where a factory one has them. Create plus AutoLearn goes straight to the proposals. The listening run stops only at slots that have a UC1 control. UF1 pages can be turned by hand outside PLUGIN mode. FX Learn follows the plug-in the HUD is on, and nothing else. EXT FUNCS are learned by clicking the slot and wiggling the control, and they are gone from the bus-comp tab, where they never applied.

**Bindings.** A press on a surface selects that button in Settings, Bindings, so you no longer hunt for it in the schematic. It can be switched off under Behaviour, and the key that opens and closes Settings keeps working, or there would be no way back out from the surface.

**REC.** Changing a track's input channel with Shift and a rotation works without an RME interface. The input is REAPER's own; only preamp gain, 48V, pad and phase need TotalReaper.

**UF8 Plug-in Mode.** A fresh REAPER never starts in it. It used to be restored from the last session, and since the mode deliberately locks the Quick and bank keys, a surface came up unable to change a soft-key set with nothing on screen saying why. A press the mode swallows now flashes the banner.

**USB.** A failed reopen is retried every five seconds instead of once, and the extension counts what floods a surface, so a UC1 that drops out leaves something to read afterwards.

## Metering: eight strips, and a calibration you capture

**Gain reduction reads per strip on all eight UF8 strips.** Seven of them had been dark since April. Gate gain reduction is there too, including the 32Classic's, the row asks the strips instead of redoing their arithmetic, and Combine GR composes with the source instead of being gated by it.

**The calibration is captured, not typed.** Drive the compressor to a point you can see, press capture, and the scale follows from that one point. The UF1's comp GR reads through the same calibration, and the columns are laid out as a table.

## Metering and the SSL protocol

A reader of this repository, [sollapse](https://github.com/acklin83/Rea-Sixty/issues/8), took the notes in this repository apart against their own captures and filed twelve findings. Ten of them are fixed here, and most are things you can see on the glass.

**The analogue needle and the overload LEDs run on the real stream again.** A meter frame that leaves the data type out means VuPpm, which is the default, and the parser here threw exactly those frames away. The needle had been emulated against that gap; it no longer has to be.

**The goniometer draws at full width.** Its payload is 17113 four-bit cells, not 8557 bytes, so half the horizontal resolution was being thrown away and a brightness ramp added on top was fighting the aliasing it caused. Both are gone.

**A setting that returns to zero arrives.** In SSL's protocol a field at its default is left out, so a value of exactly 0 looked like an absent field and the old one stood. That is also why the object ids are now checked against their own names at compile time: they are hashes of the plain text, and fourteen of them are pinned that way.

**The plug-in says what it is, and what it is sending.** The handshake carries the type, so a Meter, a Meter Pro and a channel strip are told apart instead of guessed at, and the prepare messages are read: the readouts wear the names the plug-in gives them, and a mono meter gets the mono faceplate instead of the stereo one. Only real data frames are parsed as meters now.

**HQ Mode and A/B go over the protocol** where the plug-in offers it, and fall back to the chunk where it does not. The EQ graph's last point is the curve rather than a fixed 0 dB, so the step at the 20 kHz edge is gone, and the meter ring's AUTO position says AUTO.

**Two of the twelve are still open:** the selection LED, which needs an instance identity that does not depend on the instance streaming first, and the multi-channel overview, which needs a surround Meter Pro to test against.

## Mapping, panels and colours

**EXT FUNCS read SSL's own list.** For each factory strip the entries now come from what the plug-in puts on the wire, in SSL's order, rather than from a rule of ours. The rule everyone reaches for first, "whatever is not already on a knob", is wrong: seven entries of the 32C list sit on its V-Pots at the same time. The Bus Comp has no such menu and no longer pretends to.

**One answer to the question "which plug-in is in play".** The UF8, the UF1 and the panels asked it in their own ways and could disagree with each other. There is one resolver now, the UF1 reads it, and an open plug-in window outranks a surface FX you cannot see.

**A learned channel strip lands where a factory one does.** Its soft-keys take the factory positions on the UF1 instead of filling from the top, and the Bus Comp gets the factory UF1 layout as well. The EQ graph no longer draws a strip's curve under a plug-in that has no EQ.

**FX Learn and AutoLearn, from the day at the desk:** a plug-in already mapped on the UC1 and the UF1 can be learned on the UF8 too, the UF8 fader learn no longer depends on an open plug-in window, no fader is pre-ticked anywhere, AutoLearn stops proposing the same parameter twice and says what it did, it stops asking again for the mode you just picked, and "don't show offline FX" applies to every answer instead of only the last one.

**Banners tell the truth about who caused them.** The first mode change of a run used to be swallowed as a baseline, and the Extender's own announcement could eat the one your keypress had earned.

**One more colour.** Palette index 0x0C renders on the hardware and had been thrown away for five months, so track colours quantise onto twelve entries instead of eleven. An LED override on a key's plain slot could be in force and unreachable at the same time; it is reachable.

**The focused-track panel** got proper rows and centring that centres.

## Configuration

The bindings file moves to **version 41**. Every step is additive or migrates itself: v33 gives a UF8 sub-bank its own name, v34 gives a set a name and a number, v35 to v38 move the above-fader V-Pot's push onto a real binding and drop the long press that mirrored it, v39 renames the Focus Set family from its old internal name, v40 tells the channel focus key which panel half it sits on, and v41 makes the jog content drag a hold. A configuration from v0.5.9 loads unchanged, and an unnamed set reads as *Set N*, which is what every set was before. Projects are untouched; Sticky Pot pairs are stored in the project alongside the pins that carry them.

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

---

## ⛔ BEIM TAGGEN: diesen Satz ins README zurück

Er stand dort ab `681ed8e` und war damit **öffentlich sichtbar, bevor v0.6.0
getaggt war** — auf einem Repo, dessen letzter Release v0.5.9 ist. Frank hat es
am 17.09. auf GitHub gesehen. Herausgenommen mit dem Commit, der diese Zeilen
anlegt; er gehört wieder in den Status-Absatz von `README.md`, direkt vor
„An on-screen Plug-in Mixer view is not planned.", und zwar **im selben Commit
wie der Tag**, nicht davor:

> v0.6.0 made a soft-key set a thing you own — nine of them, each with a number and a name, all in one matrix you edit in place, with copy, cut and paste of a whole bank and SSL's own two rows sitting in it like the rest — and taught the surface to reach past REAPER: **OBS** on the keys with recording, chapter marks and scenes by name, and a **Sticky Pot that carries two parameters at once**, counter-running, so an 1176 can be driven harder without the level moving.

⛔ **Die Regel daraus:** die Release-Vorbereitung darf das README nicht auf eine
Version setzen, die es noch nicht gibt. Alles andere an der Vorbereitung ist
unsichtbar (Notes-Datei, Handbuch-Versionsstrings, Bench-Liste); das README ist
das Erste, was ein Besucher sieht, und es hat keine Automatik hinter sich.

⚠ **Noch offen, Franks Entscheidung:** `docs/user-manual.md` trägt in Zeile 8
und 40 ebenfalls v0.6.0. Das Handbuch ist öffentlich im Repo UND in die Binary
eingebettet. Zurückdrehen hiesse, dass das eingebaute Handbuch im laufenden
Entwicklungsstand v0.5.9 behauptet, obwohl der Stand die v0.6.0-Funktionen hat.
Deshalb nicht angefasst.
