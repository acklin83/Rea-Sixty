# Rea-Sixty v0.8.0, "The Return of the Son of Shut Up and Play yer UF8"

**Two UF8s, and the UF8 as a side-car.** Two UF8s side by side make one surface of 16 strips. Hue, DynaMount and, with ORC, TotalMix take the whole left UF8 on Shift + LAYER 1 to 3. DynaMount gets presets.

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

## Two UF8s

- Two UF8s side by side as one surface with 16 strips. Built from Tim Shadgett's pull request (#10).
- Settings, Devices: Second UF8 (off out of the box), UF8 L and UF8 R with Identify each, Swap. Left and right follow the USB ports. Off, Rea-Sixty uses one UF8 and does not look for another.
- The right unit's soft-keys show the next page; the page keys pick the page of the unit they are pressed on, and each unit lights its own. FX and Lanes banks run on with entries 9 to 16.
- MIDI CC layer: the right unit shows the next page.
- UF8 Plug-in Mode: a 16-channel map lies across both units; any other map gets a second V-Pot bank on the right unit.
- UF1 Extender: a third place, between the two UF8s.
- Action *Swap UF8s*. Manual: new chapter Two UF8s.

## Hue and DynaMount on the UF8

- Hue and DynaMount take the whole left UF8, on Shift + LAYER 2 and 3 where those are free. The same key goes back to the mode you came from, REC and AUTO included. A right UF8 keeps showing tracks.
- Their soft-keys are the eight Hue scenes and the eight DynaMount presets; V-POT and 1-5 are dark.
- The V-Pot rings of lamps and stands show again.

## DynaMount presets

- Eight presets, each where every stand is at one moment, with a name of its own. A preset finds a stand by its IP address; a stand it does not know stays where it is.
- On a soft-key: push recalls, a long press on an empty key stores, Shift + push stores over, Ctrl + push clears.
- Soft-key bank kind *DynaMount Presets*, actions *Dyna Preset* and *Dyna Store*, and a table in the Dynamount tab.

## TotalMix on the UF8 (with ORC)

- With ORC installed, Shift + LAYER 1 puts TotalMix on the left UF8, as the UF1 side-car does: eight channels of the row the UF1 shows, one selection for both surfaces.
- Faders, V-Pots (gain, pan, width), SOLO, CUT and SEL per strip; BANK eight channels, PAGE one; the nav cross picks row, submix and the TotalMix window, as on the UF1.
- SEND/PLUG-IN 1-6 pick the submix in the order of TotalMix' control room, and the colour bar says where a fader writes: `> Phones 2`, `OUTPUT`, `FX`.
- PLUG-IN or a push on the CHANNEL encoder opens the channel view: the selected channel's STRIP pages on the V-Pots, picked on the soft-keys and stepped with PAGE. On the FX row Reverb and Echo use all eight V-Pots, and soft-key 8 switches the effect.
- V-POT and 1-5 pick the TotalMix soft-key bank, the UF1's too; OBS scenes can be a bank of their own (set up in ORC).
- The CHANNEL encoder is Main; with *Show Main volume on change* (in ORC, on out of the box) Main stands on strip 8 for two seconds while it turns.
- BANK and PAGE light where they lead somewhere.

## With ORC

- REAPER waits for ORC to let go of the UF1 and the UF8, up to three seconds, before it opens them, and takes the UF8 from ORC too.
- Hue, DynaMount and OBS stay in step with ORC: at every handover each side takes the settings the other changed since, the newer change wins. *Import from ORC* on the Hue, OBS and DynaMount tabs copies everything at once.
- The positions of the DynaMount stands travel with the handover.
- UF1 side-car: Stereo is always the fourth key of the first STRIP page.

## Fixes

- Hue: picking another scene or lamp changes its label too, unless you typed one.
- DynaMount: an import no longer turns a right fill to the left.

## Known issues

- Two UF8s is new and has not been run on two units yet. One UF8 works as before, with *Second UF8* off.
- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.8.0.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.8.0.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.8.0.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
