# Windows 11 — UF8 connects to Windows but not to SSL 360°

## Symptom
Device Manager events for UF8:
```
Device install requested
Device configured (oem9.inf)
Device started (SSLBUS)
Information: Device USB\VID_31E9&PID_0021\UF-001254 requires further installation.
```
SSL 360° shows the UF8 as disconnected / not found.

## Diagnosis
The **SSLBUS** filter/bus driver loaded successfully — it claimed the vendor-specific USB interface (`0xFF/0xFF/0xFF`) and enumerates child functional devices. "Requires further installation" means those child devices do not have their upper-level drivers (the functional SSL drivers) installed. Typical causes:

1. **SSL 360° installer was not run as Administrator** — driver INFs for child devices weren't accepted by Windows
2. **Installer was interrupted** or the OEM driver-signing prompt was dismissed
3. **Stale previous install** — a half-installed earlier SSL 360° version left conflicting INFs (oem9.inf is the current one, but older oemN.inf files may confuse things)
4. **UF8 firmware** too old for current SSL 360° build (unlikely on a fresh Windows, but possible)

## Fix — full clean reinstall (the reliable path)

### 1. Remove SSL 360° and all SSL drivers
- Settings → Apps → **Uninstall SSL 360°**
- Unplug the UF8
- Device Manager → View → **Show hidden devices** → find any remaining "SSL Control I/F" / "SSL USB" / UF8 entries → right-click → Uninstall device → **check "Delete driver software"**
- (optional but clean) `pnputil /enum-drivers | findstr /i "ssl"` in an admin shell, then `pnputil /delete-driver oemN.inf /uninstall /force` for each listed SSL INF
- Reboot

### 2. Download current SSL 360°
- https://www.solidstatelogic.com/support-page/uf8-downloads → latest Windows build (needs to be **6.3 or newer** for Windows 11)

### 3. Install as Administrator
- Right-click the installer → **Run as administrator**
- When Windows shows "Do you want to install this device software?" for Solid State Logic → **always Install**, never Don't Install
- Let the installer finish fully (UAC + multiple driver signing prompts)

### 4. Reconnect UF8
- Plug UF8 into a **direct USB port on the motherboard** (not a hub, not a monitor's USB, not a Thunderbolt dock)
- Device Manager should now show the UF8 under a new category (usually "Solid State Logic" or under "Universal Serial Bus devices") **without** the warning triangle and without "requires further installation"

### 5. Launch SSL 360° as Administrator the first time
- Right-click SSL 360° shortcut → Run as administrator
- It should now detect the UF8 and offer to register / authorize

### 6. Verify before capturing
- UF8 display shows meters or channel names → SSL 360° has connected
- In SSL 360°, load Plugin Mixer page, make sure at least one SSL Channel Strip is loaded on a REAPER track
- Move a fader or change a track color → UF8 display reacts → colors are flowing over USB → we're ready to capture

## If still "requires further installation" after the clean reinstall

Likely a driver-signing issue. In an **admin PowerShell**:
```powershell
Get-WindowsDriver -Online | Where-Object { $_.ProviderName -like "*Solid State*" }
```
to list installed SSL drivers. If the functional drivers (not SSLBUS, but the child-device drivers) are missing, the installer isn't placing them. Check Windows Event Viewer → Applications and Services Logs → Microsoft → Windows → DriverFrameworks-UserMode for the error cause.

Last-resort: Solid State Logic support — they have a "driver cleanup tool" they share on request.

## Reference — driver file names
- `oem9.inf` (or similar oemN.inf, number varies) = the SSL bus driver. This one loaded.
- There should be an additional `oemM.inf` for the functional devices that's currently **missing** in the failure case.

---

# UF8 stays on WinUSB after uninstall — SSL 360° still doesn't see it

## Symptom
You uninstalled the Rea-Sixty WinUSB driver (and/or reinstalled SSL 360°),
but SSL 360° still shows the UF8/UC1 as disconnected. The tell: if you
**enable Rea-Sixty in REAPER's Control/OSC prefs, it picks the UF8 right
back up.**

## Diagnosis
The device is **still bound to WinUSB (the Rea-Sixty driver), not to
SSLBUS.** WinUSB and SSLBUS are **mutually exclusive** on the UF8's
vendor interface — only one can own it. Rea-Sixty can only claim the UF8
through WinUSB/libusb, so the fact that it still grabs the device proves
WinUSB is still bound. That is exactly why SSL 360° sees nothing.

Reinstalling SSL 360° does **not** forcibly take the binding back if the
WinUSB binding is still sitting on the device — the device has to be
re-enumerated against the SSLBUS driver.

## How to check
1. **Quit REAPER and SSL 360°** (so nothing is holding the device), UF8
   plugged into a **direct motherboard USB port**.
2. Device Manager → find the UF8 → right-click → **Properties → Driver
   tab → Driver Provider**:
   - `Microsoft` / driver = **WinUSB** → still on the Rea-Sixty binding
     (this is the problem).
   - `Solid State Logic` / **SSLBUS** → correctly reverted.
3. Or, in an **admin shell**:
   ```
   pnputil /enum-drivers | findstr /i "rea_sixty_winusb"
   ```
   If it still lists an `oemN.inf` with original name
   `rea_sixty_winusb.inf`, the driver is still in the store.

## Fix — fully revert to SSLBUS
1. Quit REAPER **and** SSL 360°.
2. Device Manager → **View → Show hidden devices** → find UF8 (and UC1 if
   present) → right-click → **Uninstall device** → tick **"Delete the
   driver software for this device"** → confirm.
3. Admin shell — remove any leftover store entry:
   ```
   pnputil /enum-drivers | findstr /i "rea_sixty"
   ```
   For each `oemN.inf` it shows:
   ```
   pnputil /delete-driver oemN.inf /uninstall /force
   ```
4. **Unplug + replug** the UF8. Windows re-enumerates and SSLBUS
   (reinstalled with SSL 360°) should now claim it.
5. Launch **SSL 360° as Administrator** the first time.

## Confirmation test
After the revert, enable Rea-Sixty in REAPER's Control/OSC prefs again. It
should **no longer** be able to pick up the UF8 — it can't claim the
device once SSLBUS owns it. That "it can't grab it anymore" is the proof
the binding flipped back to SSL.

> **macOS note:** the WinUSB swap is Windows-only. On macOS there is no
> kernel driver to uninstall — Rea-Sixty claims the device directly via
> libusb. The only conflict there is *both apps running at once*: fully
> quit REAPER (or remove the Rea-Sixty surface) and SSL 360° will see the
> UF8.
