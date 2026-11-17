# Rea-Sixty v0.9.3, "Reservoir Jogs"

**Four jog modes of your own on the UF1, and the UF1 fader can go with the jog mode: track, item volume, take volume or the last touched parameter.**

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

## What's new

- **Your own jog modes on the UF1:** *User 1* to *User 4*, hidden until you tick them under Settings, Bindings, UF1, JOG WHEEL. Each gets a name of up to 8 characters, shown on the UF1 and in the mode banner.
  - *Left / right*: each detent to the left fires the *Turn left* action, each detent to the right the *Turn right* action.
  - *One action with direction*: the action gets the detents with their direction, the way REAPER takes a MIDI controller set to relative. For REAPER's actions marked *(MIDI CC relative/mousewheel)*, such as horizontal zoom or vertical scroll.
  - *Detents per step* sets how many detents make one step, 1 to 64.
  - `SHIFT` without an action of its own runs the plain one, finer by *Shift fine div*.
  - The nav cross has its own keys in each of the four.
  - Four new actions put the wheel on them: *UF1 Jog Mode: User 1* to *User 4*.
- **The UF1 fader per jog mode:** every jog mode has a menu for the fader.
  - *Track*: what the fader always does. Factory setting.
  - *Item volume*: every selected item, the first follows the fader, the others keep their difference in dB.
  - *Take volume*: the same for the active take of every selected item. A take with its polarity flipped keeps it.
  - *Last touched parameter*: the plug-in parameter you touched last, with Touch automation as on the strip.
  - The channel display names what the fader moves: *Item Vol*, *Take Vol* or the parameter's name. With nothing to move it reads *no item*, *no take* or *no param*, and the fader stays where it is.
  - What the fader moves is fixed while your hand is on it. Each grab on item or take volume is one undo step.
  - An Items row with *Fader = Item Volume* ticked in v0.9.2 comes over as *Item volume*.
- **The jog mode list** under Settings, Bindings, UF1, JOG WHEEL is a table with the columns Show, Order, Mode and Fader. *UF1 Jog Mode: cycle* steps only through the ticked modes, as `SCRUB` and the wheel do.
- **Items jog mode:** on a track with fixed lanes the nav cross selects only items on lanes that play.
- **Fix:** a key with *Settings* on its long press, such as `360`, closes Settings again while the Bindings pane is open. Until now the long press picked the key for editing.
- **Fix:** with ORC running before REAPER started, the UF8 tab under Bindings could be missing.

## Known issues

- Two UF8s has not been run on two units yet. One UF8 works as before, with *Second UF8* off.
- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.
- Many REAPER actions make an undo step each time they run, so one turn of the jog in a user mode can make a lot of them.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.9.3.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.9.3.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.9.3.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
