#!/usr/bin/env bash
# stoerpc_capture.sh — drive a fixed-window USBPcap capture on the StoerPC (Mac -> SSH)
# and pull the pcap back into captures/. Persistent tooling (analysis/) so it survives
# the session — cap.sh kept getting rebuilt from scratch every time (memory note).
#
# Usage:
#   analysis/stoerpc_capture.sh NAME [SECS] [IFNUM]     # real capture, SECS default 40
#   analysis/stoerpc_capture.sh --test [IFNUM]          # 4 s preflight: confirms the
#                                                         # interface carries UF1 traffic
# Then decode with:  python3 analysis/uf1_loudness_vpot_decode.py captures/NAME.pcap
#
# Rig (memory HANDOFF-uf1-loudness): StoerPC LAN 192.168.177.198 (DHCP — re-check!),
# UF1 = VID_31E9 PID_0025 on sslbus, meter traffic on \\.\USBPcap3, SSL 360 + REAPER +
# SSL Meter Pro driving it. The remote shell is CMD, so PowerShell goes via
# -EncodedCommand (UTF-16LE base64) — this is what keeps the \\.\USBPcap3 backslashes
# intact (an unquoted bash heredoc ate them last time -> 0-byte capture) and dodges CMD
# pipe/quote leakage. USBPcapCMD needs -A or it prompts interactively and hangs.
set -euo pipefail

IP="${STOERPC_IP:-192.168.177.198}"
USER=claude
PASS=claudepass
SSH=(sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no -o ConnectTimeout=8 "$USER@$IP")
SCP=(sshpass -p "$PASS" scp -o StrictHostKeyChecking=no)
HERE="$(cd "$(dirname "$0")/.." && pwd)"      # repo root

psenc() { python3 -c "import sys,base64;print(base64.b64encode(sys.stdin.read().encode('utf-16-le')).decode())"; }

# Build the remote PowerShell: start USBPcapCMD hidden on \\.\USBPcap<IFNUM>, capture
# for <SECS>, stop, report the byte size. The static body is a QUOTED heredoc (bash
# leaves the backslashes + $-vars alone); the dynamic values are prepended as PS
# assignments, so nothing dynamic ever touches the backslash path.
remote_capture() {
  local name="$1" secs="$2" ifnum="$3"
  local body
  body=$(cat <<'PS'
$ErrorActionPreference='Continue'
$ProgressPreference='SilentlyContinue'
$dev = "\\.\USBPcap$IFNUM"
$out = "C:\Users\claude\ufcap_$NAME.pcap"
if (Test-Path $out) { Remove-Item $out -Force }
$p = Start-Process -FilePath "C:\Program Files\USBPcap\USBPcapCMD.exe" -ArgumentList @('-d', $dev, '-o', $out, '-A') -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $SECS
try { Stop-Process -Id $p.Id -Force } catch {}
Start-Sleep -Milliseconds 600
if (Test-Path $out) { Write-Output ("BYTES=" + (Get-Item $out).Length) } else { Write-Output "BYTES=0 (no file - wrong interface?)" }
PS
)
  local full="\$IFNUM=$ifnum; \$SECS=$secs; \$NAME='$name'; $body"
  local b64; b64=$(printf '%s' "$full" | psenc)
  "${SSH[@]}" "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $b64"
}

if [[ "${1:-}" == "--test" ]]; then
  IFNUM="${2:-3}"
  echo ">> Preflight: 4 s test capture on \\\\.\\USBPcap$IFNUM (no UF1 action needed)…"
  remote_capture "preflight" 4 "$IFNUM"
  "${SCP[@]}" "$USER@$IP:C:/Users/claude/ufcap_preflight.pcap" "$HERE/captures/_preflight.pcap"
  echo ">> FF67 (SSL->UF1) frames in the preflight:"
  python3 "$HERE/analysis/uf1_loudness_vpot_decode.py" "$HERE/captures/_preflight.pcap" --count || true
  echo ">> If the FF67 count is > 0, USBPcap$IFNUM is the right interface. Else try another IFNUM."
  exit 0
fi

NAME="${1:?usage: stoerpc_capture.sh NAME [SECS] [IFNUM]  |  --test [IFNUM]}"
SECS="${2:-40}"
IFNUM="${3:-3}"
echo ">> Capturing '$NAME' for ${SECS}s on \\\\.\\USBPcap$IFNUM."
echo ">> PAGE the UF1 Loudness screen 8 -> 9 -> 10 NOW (dwell ~3 s per page, turn a V-Pot on each)."
remote_capture "$NAME" "$SECS" "$IFNUM"
"${SCP[@]}" "$USER@$IP:C:/Users/claude/ufcap_$NAME.pcap" "$HERE/captures/$NAME.pcap"
echo ">> Pulled -> captures/$NAME.pcap"
ls -la "$HERE/captures/$NAME.pcap"
echo ">> Decode:  python3 analysis/uf1_loudness_vpot_decode.py captures/$NAME.pcap"
