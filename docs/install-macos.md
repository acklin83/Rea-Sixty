# Installing Rea-Sixty on macOS

The shipped distribution is three dylibs that go into REAPER's
UserPlugins folder. **No Homebrew install needed.**

## Files
```
reaper_rea-sixty.dylib
libusb-1.0.0.dylib
libhidapi.0.dylib
```
The main dylib's install names point at `@loader_path` so the deps
are picked up from the same folder.

## Install
Drop all three into:
```
~/Library/Application Support/REAPER/UserPlugins/
```

## Prerequisites in REAPER
- Install **ReaImGui** via ReaPack (Extensions → ReaPack → Browse
  packages → ReaImGui → Install). Without it, hardware bindings
  still work but the Settings window stays empty.
- Quit SSL 360° **and the `SSL360Core` background daemon** (Activity
  Monitor → search "SSL360Core" → kill). They claim the UF8/UC1
  vendor-USB interface exclusively.

## Verify
REAPER → Action List (`?`) → "Rea-Sixty: Open / Close Rea-Sixty
Settings". If the action is missing, the dylib didn't load — check
that all three files are in UserPlugins.

## Uninstall
Remove the three files from UserPlugins, restart REAPER.

---

## Building from source (developers only)

```bash
brew install libusb hidapi
cd reaper-uf8/extension
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(sysctl -n hw.ncpu)
```

CMake's post-build step copies libusb / hidapi next to
`reaper_rea-sixty.dylib` and rewrites their install names to
`@loader_path/...`, so `build/` already contains the shipped trio.

## Failure modes
- REAPER → View → Console shows `Rea-Sixty UF8: SSL360Core owns the device`
  → quit SSL 360° + SSL360Core daemon, restart REAPER.
- Action "Rea-Sixty: Open / Close Rea-Sixty Settings" missing
  → all three dylibs need to be in UserPlugins together. Missing
  `libusb-1.0.0.dylib` or `libhidapi.0.dylib` next to the main
  dylib will cause the load to fail silently.
- Settings window stays blank
  → ReaImGui isn't installed. Install via ReaPack.
