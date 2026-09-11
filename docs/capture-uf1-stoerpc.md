# UF1 capture flow — StoerPC (READ THIS FIRST, don't rediscover the rig)

The one place the whole SSL→UF1 USBPcap capture flow is written down. Every past session
burned ~10 min re-deriving the IP, the shell quirks, the USBPcapCMD flags and rebuilding
throwaway scripts. Don't. The scripts are persistent (`analysis/`), the traps are below.

## Rig (resolve the IP, never remember it)
| Thing | Value |
|---|---|
| StoerPC LAN | **DHCP, it moves.** `/Applications/Tailscale.app/Contents/MacOS/Tailscale status \| grep stoerpc` says online/offline AND the current LAN IP in one line. `stoerpc_capture.sh` resolves it the same way; `STOERPC_IP=…` overrides. |
| SSH | `ssh -o UserKnownHostsFile=/tmp/kh_stoerpc_win -o StrictHostKeyChecking=accept-new claude@<ip>` — **key auth** (`~/.ssh/id_ed25519`, since 2026-05-29; `claudepass` is dead). Scratch known_hosts because the dual-boot box has one host key per OS. Remote shell is **CMD**. |
| UF1 | `USB\VID_31E9&PID_0025\UF1-009184`. Must sit on **SSL's driver** (`sslbus.inf`, provider "Solid State Logic", was `oem16.inf` on 2026-09-11) for SSL 360 to drive it. On OUR WinUSB (`rea_sixty_winusb.inf`, provider "Rea-Sixty", `oem2.inf`) 360 cannot see it. Read the binding first: `pnputil /enum-devices /instanceid "USB\VID_31E9&PID_0025\UF1-009184"`. Swap to SSL: `pnputil /delete-driver oem2.inf /uninstall` + `pnputil /scan-devices` (Frank's call each time — it takes the UF1 away from Rea-Sixty on that box). |
| Our DLL on the box | `C:\Users\sunny\AppData\Roaming\REAPER\UserPlugins\reaper_rea-sixty.dll` — rename to `.OFF` for the capture, or its impersonator competes with 360 for the plug-ins. |
| Capture IF | **`\\.\USBPcap3`** (default). Can shift per boot → the preflight confirms it. |
| USBPcapCMD | `C:\Program Files\USBPcap\USBPcapCMD.exe`. Writes standard `.pcap`. |
| Test signals | `C:\Users\Public\rea-sixty-capture\uf1_needle.wav` (52 s, three IEC tones) + `uf1_led.wav` (43 s, hits at Ref+12/+15) — from `analysis/gen_uf1_needle_wav.py` / `gen_uf1_led_wav.py`. |

## The two scripts (persistent — `analysis/`, git-tracked)
- **`analysis/stoerpc_capture.sh`** — drives the capture from the Mac over SSH, pulls the
  pcap into `captures/`.
- **`analysis/uf1_loudness_vpot_decode.py`** — decodes SSL→UF1 `0x010e` V-Pot label groups
  into per-page V2/V3/V4 param assignments (classified against the Meter Pro dump).

## Flow (start to finish)
```bash
# 0. sanity: is the box up? Ping proves nothing (Windows drops ICMP) — ask Tailscale.
/Applications/Tailscale.app/Contents/MacOS/Tailscale status | grep stoerpc

# 1. PREFLIGHT — 4 s, no UF1 action. Confirms USBPcap3 carries UF1 traffic.
analysis/stoerpc_capture.sh --test 3
#   -> prints "FF67 frames: N". N>0  => interface is right. N==0 => try  --test 1 / 2 / 4.

# 2. REAL capture. Announce, then WAIT for Frank's explicit "los" ([[capture-go-word-not-setup]]).
#    On "los": run it, and Frank pages the UF1 during the window.
analysis/stoerpc_capture.sh loud_pages_8_10 40 3
#    ^name           ^secs ^ifnum
#    During the 40 s: page the UF1 Loudness screen 8 -> 9 -> 10, dwell ~3 s each,
#    turn a V-Pot on each page so every slot emits its label.

# 3. DECODE
python3 analysis/uf1_loudness_vpot_decode.py captures/loud_pages_8_10.pcap
#    -> chronological distinct V-Pot pages, each slot tagged [param N] or [?? UNKNOWN].
#       Map the ?? slots (expected: 40 ShortTermMax, 41 MomentaryMax, 42/43 LoudRange,
#       44/45 DialogueRange, 50 Play/Pause) into kUf1LoudnessVPots + bump the page count.
#    --raw   = every distinct (index,value), unclassified   |   --count = FF67 frame count
```

## Traps (all already handled inside `stoerpc_capture.sh` — here so nobody re-learns them)
1. **Remote shell is CMD**, so PowerShell goes via `powershell -EncodedCommand <UTF-16LE base64>`.
   This dodges CMD pipe/quote leakage (`|` in a regex split the command last time) **and**
   keeps the `\\.\USBPcap3` backslashes intact — an unquoted bash heredoc ate them → the
   device became `\.\USBPcap3` → 0-byte capture. The script uses a QUOTED heredoc for the PS
   body and prepends the dynamic values (`$NAME/$SECS/$IFNUM`) as PS assignments.
2. **USBPcapCMD needs `-A`** (capture all devices on the root). Without it, it prompts for a
   device interactively and **hangs forever** (kill with `taskkill /F /IM USBPcapCMD.exe /T`).
3. **The capture must run inside a LIVE ssh session** (fixed `Start-Sleep`), not detached —
   a detached `Start-Process` is orphan-reaped when the SSH session closes.
4. **Force-stop is fine** — `Stop-Process -Force` closes a valid pcap (cap120/121/122 decoded
   cleanly this way).
5. pcaps are gitignored; they land in `captures/`. Decode/analysis lives in `analysis/`.

## What this was for (2026-07-24)
Finishing the UF1 Loudness V-Pot table: cap109 captured 7 of 10 pages (its 115 s window
closed at page 7). Pages 8-10 (params 40-45 max/range alerts + 50 Play/Pause) need the
capture above. Context: [docs/HANDOFF-uf1-loudness.md](HANDOFF-uf1-loudness.md).

## ⛔ USBPcap and SSL's driver — the hour of 2026-09-11, never again
USBPcap installs itself as a **class upper filter of class USB only**. SSL's
drivers (`sslbus.inf` = oem16, `ftdibus.inf` = oem13) put the UF1 control node
(`USB\VID_31E9&PID_0025\UF1-009184`) into class `SSLUSBDriver_sc`, and after any
driver rebind (pnputil delete/scan/restart, even a reboot) the node's stack was
`SSLBUS > USBHUB3` — **no USBPcap** — while the UF1's HID function, the hubs and
every plain USB device carried `… > USBPcap > USBHUB3`. Result: USBPcap "knows"
the device (`--inject-descriptors` lists it) and captures zero bytes from it.

Check, one line: `(Get-PnpDeviceProperty -InstanceId "USB\VID_31E9&PID_0025\UF1-009184" -KeyName DEVPKEY_Device_Stack).Data`
Fix, then a **physical unplug/replug** (`pnputil /restart-device` does NOT rebuild the stack):
```
New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_31E9&PID_0025\UF1-009184" -Name UpperFilters -PropertyType MultiString -Value @("USBPcap") -Force
```
After the replug the stack reads `SSLBUS > USBPcap > USBHUB3` and the capture
carries the device on OUT 0x02 / IN 0x81 as always (cap129).

Two more traps from the same hour:
- `pnputil /delete-driver oem13.inf /uninstall` (the 2.1.12 ftdibus package) also
  deletes `C:\Windows\System32\ftd2xx.dll` and `SysWOW64\ftd2xx.dll`; SSL 360 then logs
  "Ftdi unable to find library" and discovers no devices at all. Backup of the package:
  `C:\Users\claude\drv_backup_ftdibus` (restore the DLLs, or `pnputil /add-driver … /install`).
- Our own WinUSB INF leaves `DeviceInterfaceGUIDs` in the node's Device Parameters;
  `pnputil /remove-device` + `/scan-devices` recreates the node clean.
- The 360 Core log answers "is 360 talking to the UF1" in one grep:
  `C:\Users\sunny\AppData\Local\SSL\SSL360\LogFiles\CurrentRun\SSL360Core_*.log`
  ("responded to GetIsTile", "module 0 has firmware 33109").

## Run plan 2026-09-11 — after SSL 360 2.1.12 (Meter Pro 1.3.7 dropped VuPpm)
Versions on the box that day: 360 2.1.12.72214, Meter Pro 1.3.7, CS2 2.10.6,
4K B 1.10.2, 4K E 1.7.1, 4K G 1.3.1, Harrison 32C 2.0.20, REAPER 7.66.
Each window opens only on Frank's explicit "los".

| # | name | UF1 state | Frank does | answers |
|---|---|---|---|---|
| 129 | `cap129_uf1_vu_needle_led` | Meter, Analogue screen, **VU** | play `uf1_needle.wav`, then `uf1_led.wav` | second needle (0x0127) fall law; LED mask (0x0128) flash vs latch |
| 130 | `cap130_uf1_ppm_needle_led` | same, **PPM** | same | same for PPM |
| 131 | `cap131_uf1_pim_daw` | Layer 3 Plug-in Mixer, PLUG-IN key unlit (DAW) | press CHANNEL encoder (mode list), turn a pan, page soft keys, PLUG-IN/DAW toggle | pan readout on 7-seg, mode list, SEND A/B labels, host tab |
| 132 | `cap132_uf1_pim_cs` | Plug-in Mixer, PLUG-IN lit, CS2 or 4K E focused | page soft keys 1..8, turn each V-Pot briefly per page, bypass | new plug-in-mode display vs ours |
| 133 | `cap133_uf1_32c` | Plug-in Mixer, Harrison 32C focused | all soft-key pages, every V-Pot per page | 0x0017 CS TYPE "32C", 0x010e labels, 0x0104 soft keys → factory strip |

Decode: `analysis/uf1_vu_needle_fit.py` (0x0125/0x0127 vs readout), `analysis/uf1_screen_dump.py`
(every zone incl. 0x0128 and the new elements), `analysis/uf1_loudness_vpot_decode.py --raw`
(0x010e V-Pot pages, adapt the param table for the 32C).
