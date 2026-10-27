# Rea-Sixty v0.7.0, "Easy MIDI Rider"

**The MIDI CC layer.** Any of the UF8's three layers can send MIDI: faders, V-Pots, V-Pot pushes, the soft-keys and both foot switches send Control Change, the SEND/PLUG-IN keys Program Change, to REAPER's virtual MIDI keyboard and to hardware outputs. It works in both directions: while REAPER plays, the faders follow the CC lanes of the selected track, and a synth that reports its knobs moves the UF8 with them.

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

## MIDI CC layer (UF8)

- **Layer type** in Settings, Bindings, UF8: set a layer to *MIDI CC* and the schematic gives way to the CC editor. Set it back to *REAPER* and the CC set stays for later.
- **Output:** REAPER's virtual MIDI keyboard, so an armed instrument track records it, and any MIDI output REAPER lists, several at once.
- **Six pages**, picked with the soft-key bank keys V-POT and 1 to 5 or stepped with PAGE.
- **Per control:** channel, CC number or program, name (8 characters, shown on the UF8) and colour. Keys toggle 0/127 or send 127 while pressed. Faders on CC 0 to 31 can send 14 bit (the controller and the one 32 above it).
- **Back to the surface:** with *Follow CC in REAPER* on, faders and rings follow the CC lanes of the selected track while REAPER plays or records. A MIDI *Input* moves them with a synth's own knobs. A touched fader is not moved from outside.
- **On the UF8:** each strip shows the fader's name and value, the V-Pot's name and value, and the page; SEL and the colour bar take the fader's colour. FLIP swaps faders and V-Pots.
- **CC presets:** save the layer's set under a name and load it into any CC layer. *Default* restores the factory set: CC1 (14 bit), CC11, CC2, CC21, CC7, CC10, CC74 and CC71 on the faders, sustain and soft pedal on the foot switches.
- FINE, the channel encoder and its keys, the zoom pad, the automation row, FLIP, the layer keys and 360 keep working as on Layer 1.

## Banking

- **Bank follows REAPER selection** (Settings, Bindings, UF8; also a switch for a key and a REAPER action). Off, a track you select in REAPER leaves the bank where it is; a selection made on the surface still banks. On by default.

## Fixes

- **UC1 input meter** on a track with fixed lanes reads only the lanes that play. Items on silent lanes were added in and read far too loud.
- **UF1** small-display comp GR follows *GR meter source*: with *Only Show Channel Strip GR*, a compressor that is not a mapped channel strip no longer lights it.

## Manual

- New chapter: MIDI CC layer (UF8).
- Described now: the UF1 soft-key LEDs as Bus Comp GR meter, and *Scroll banks*.

## Known issues

- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.7.0.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.7.0.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.7.0.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
