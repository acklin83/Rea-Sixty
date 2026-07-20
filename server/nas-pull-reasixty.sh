#!/bin/sh
# Pull the newest mapping-exchange backup from the VPS onto the Synology.
#
# Runs ON THE NAS, from DSM's Task Scheduler (Control Panel -> Task Scheduler ->
# Create -> Scheduled Task -> User-defined script), as user root, nightly a
# little after the VPS's own 03:17 UTC backup.
#
# The VPS never reaches out to the NAS. It holds no credential pointing here,
# so taking the VPS does not get you the backups. The key this script uses is
# locked to a forced command on the far end (/usr/local/bin/reasixty-backup-
# fetch): it can fetch the newest backup and list run names, and nothing else —
# no shell, no scp, no other path. Verified, not assumed.
#
# ⚠ THE VPS IS ADDRESSED BY ITS PUBLIC HOSTNAME, NOT ITS TAILSCALE IP, AND
# THAT IS DELIBERATE. The VPS runs Tailscale SSH (`RunSSH: true`), which
# intercepts port 22 on the tailnet address and authenticates by tailnet
# identity — the connection never reaches the system sshd, so authorized_keys
# and the forced command with it are simply not consulted. Over the tailnet
# this script does not merely lose its restriction, it breaks: with no forced
# command, `ssh … > file` opens a login shell instead of writing a tar. Going
# through the public hostname puts the system sshd back in the path. The
# traffic is SSH either way and port 22 is publicly reachable regardless.

set -eu

VPS=${VPS:-srv1401714.hstgr.cloud}             # NOT the tailnet IP — see above
KEY=${KEY:-/volume1/homes/Frank/.ssh/reasixty_nas_key}
DEST=${DEST:-/volume1/Franks Cloud/Backups/reasixty}
KEEP_DAYS=${KEEP_DAYS:-90}

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT=$DEST/reasixty-$STAMP.tar.gz

mkdir -p "$DEST"

# Write to a .part first: ssh propagates the far end's exit status, so a failed
# or truncated transfer must not be left sitting there looking like a backup.
if ssh -i "$KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
       "reasixty-backup@$VPS" > "$OUT.part"; then
    mv "$OUT.part" "$OUT"
else
    rc=$?
    rm -f "$OUT.part"
    echo "reasixty backup pull FAILED (ssh exit $rc)" >&2
    exit "$rc"
fi

# A tar that will not list is a tar that will not restore. Cheaper to find out
# now than on the day it is needed.
if ! tar tzf "$OUT" >/dev/null 2>&1; then
    echo "reasixty backup pull produced an unreadable archive: $OUT" >&2
    rm -f "$OUT"
    exit 1
fi

find "$DEST" -maxdepth 1 -type f -name 'reasixty-*.tar.gz' -mtime "+$KEEP_DAYS" -delete

echo "reasixty backup pulled: $OUT ($(du -h "$OUT" | cut -f1)), keeping ${KEEP_DAYS}d"
