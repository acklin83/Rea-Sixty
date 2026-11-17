# Session 17.11.2026: UF8 tab missing while ORC runs

## Report (forum)
With ORC running before REAPER, Settings > Bindings has no UF8 tab. REAPER first, then ORC: tab stays. Quit ORC, restart REAPER: tab back. The UF8 itself works.

## Cause
The Bindings tabs show a surface only when it is in the device inventory (`reasixty_settingsShowsDevice`, ExtState `rea_sixty/devices_seen`).
1. `initDevices_` opens the devices before `loadBrightness` reads `devices_seen`. `markDeviceSeen_` merged only with the memory mask (still 0) and wrote it, so a UC1 opening at start overwrote a stored UF8 + UC1 with UC1 alone.
2. A UF8 that ORC hands over later opens through the timer's retry path, which never called `markDeviceSeen_`. Same gap on the UC1 retry path.

## Fix `696d1a7c`
- `src/DevicesSeen.h`: `devseen::mark(stored, known, bit)`, merges with the stored value.
- `markDeviceSeen_` reads ExtState first.
- UF8 and UC1 reopen paths mark their surface.
- `tests/test_devices_seen.cpp` replays the start order. ctest 49/49.

Workaround for older builds: Settings > General, "Show settings for devices you don't have".

## Also since the jog-modes note (16.11.)
- `5cdc005e` user jog mode names in the list row.
- `99a3dde6` Bindings press-to-pick lets the Settings key through on any gesture (Frank's 360 long press).

## Open
- Frank: the two behaviour changes from the jog modes ("Item Vol" zone name, cycle walks only ticked modes), keep or revert.
- Not released. Dylib 53123e6a installed, needs a REAPER restart.

## Jog modes list as a table `04b4b998`
Show / Order / Mode / Fader columns with headers. A missing arrow is the same small button, dimmed and inert, so the names line up.

## Item arrows on fixed lanes `7064f17c`
Frank: in Items jog mode the arrows picked items from another lane than the active one. `uf1SelectAdjacentItem_` and `uf1NearestItemOnTrack_` never checked the lane (unchanged since 15.09.; fixed lanes came 01.10.). They now skip items whose lane does not play, via `itemLanePlays_` (shared with the UC1 input meter). Group mates that REAPER's 40034 pulls in are not filtered.

## Nachtrag: Tabelle ohne graue Zeilen
RowBg raus, Kopfzeile ohne Fuellung, Spalten 36/56/96/136 statt 44/56/130/190 (Frank: graue Zeilen sehen nicht gut aus, zu breit).
