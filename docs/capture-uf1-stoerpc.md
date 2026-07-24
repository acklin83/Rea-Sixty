# UF1 capture flow — StoerPC (READ THIS FIRST, don't rediscover the rig)

The one place the whole SSL→UF1 USBPcap capture flow is written down. Every past session
burned ~10 min re-deriving the IP, the shell quirks, the USBPcapCMD flags and rebuilding
throwaway scripts. Don't. The scripts are persistent (`analysis/`), the traps are below.

## Rig (verify only the IP — the rest is stable)
| Thing | Value |
|---|---|
| StoerPC LAN | **`192.168.177.198`** — DHCP, **re-check first** (`ping`; or scan the /24 for :22). NOT the Tailscale IP ([[tailscale-ssh-bypasses-authorized-keys]]). |
| SSH | `sshpass -p claudepass ssh -o StrictHostKeyChecking=no claude@<ip>` — **remote shell is CMD**, host key changes per boot (StrictHostKeyChecking=no handles it). |
| UF1 | `USB\VID_31E9&PID_0025\UF1-009184`, on **sslbus**, driven by SSL 360 + REAPER + SSL Meter Pro. |
| Capture IF | **`\\.\USBPcap3`** (default). Can shift per boot → the preflight below confirms it. |
| USBPcapCMD | `C:\Program Files\USBPcap\USBPcapCMD.exe`. Writes standard `.pcap`. |

## The two scripts (persistent — `analysis/`, git-tracked)
- **`analysis/stoerpc_capture.sh`** — drives the capture from the Mac over SSH, pulls the
  pcap into `captures/`.
- **`analysis/uf1_loudness_vpot_decode.py`** — decodes SSL→UF1 `0x010e` V-Pot label groups
  into per-page V2/V3/V4 param assignments (classified against the Meter Pro dump).

## Flow (start to finish)
```bash
# 0. sanity: is the box up? (only the IP drifts)
ping -c2 192.168.177.198

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
