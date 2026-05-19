# Installing Rea-Sixty on macOS

Short reference. The full procedure incl. troubleshooting lives in
`user-manual.md` chapter 2.

## Prerequisites
- `brew install libusb hidapi`
- REAPER
- **ReaImGui** from ReaPack (needed for the Settings window)
- UF8 / UC1 plugged in
- SSL 360° quit, including the `SSL360Core` background daemon

## Build
```bash
cd reaper-uf8/extension
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(sysctl -n hw.ncpu)
```
Output: `build/reaper_rea-sixty.dylib`.

## Install into REAPER
```bash
cp build/reaper_rea-sixty.dylib \
   ~/Library/Application\ Support/REAPER/UserPlugins/
```
Or symlink during development:
```bash
ln -sf "$PWD/build/reaper_rea-sixty.dylib" \
       ~/Library/Application\ Support/REAPER/UserPlugins/reaper_rea-sixty.dylib
```

Restart REAPER. Run action **"Rea-Sixty: Open / Close Rea-Sixty
Settings"** from the Action List (`?`) to verify the surface loaded.

## Uninstall
```bash
rm ~/Library/Application\ Support/REAPER/UserPlugins/reaper_rea-sixty.dylib
```
Restart REAPER.

## Failure modes
- REAPER → View → Console shows `Rea-Sixty UF8: SSL360Core owns the device`
  → quit SSL 360° + SSL360Core daemon, restart REAPER.
- Action "Rea-Sixty: Open / Close Rea-Sixty Settings" doesn't show up
  → DLL didn't load. Usually missing libusb / hidapi (`brew install`)
  or the dylib not in the UserPlugins folder.
- Settings window stays blank
  → ReaImGui isn't installed. Install via ReaPack.
