#!/bin/sh
# Serve the newest backup run as a tar stream on stdout.
#
# Installed on the VPS at /usr/local/bin/reasixty-backup-fetch and wired as the
# FORCED COMMAND of the `reasixty-backup` account's SSH key, so that key can do
# this and nothing else — no shell, no file writes, no other path.
#
# WHY PULL AND NOT PUSH. If the VPS pushed to the NAS it would have to hold a
# credential that can write to the NAS, and anyone who takes the VPS could then
# destroy the backups too. Pulling inverts that: the VPS holds no outbound
# credential, and the worst this key grants is a copy of data the holder of the
# VPS already has.
#
# WHY A TAR STREAM AND NOT rsync/scp. The Synology's rsync has already refused
# to cooperate once in this setup (scp -O was needed instead), and DSM's task
# scheduler is a plain shell. A tar on stdout needs nothing at the far end but
# an ssh client:
#
#   ssh -i <key> reasixty-backup@<vps> > reasixty-$(date -u +%Y%m%dT%H%M%SZ).tar.gz
#
# `ssh … list` prints the run directories instead, for checking by hand.

set -eu

DEST_DIR=${DEST_DIR:-/opt/reasixty/backups}

runs() {
    find "$DEST_DIR" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
}

case "${SSH_ORIGINAL_COMMAND:-latest}" in
    ''|latest) ;;
    list)
        runs
        exit 0
        ;;
    *)
        echo "reasixty-backup-fetch: only 'latest' (the default) and 'list' are allowed" >&2
        exit 2
        ;;
esac

RUN=$(runs | tail -1)
if [ -z "$RUN" ]; then
    echo "reasixty-backup-fetch: no backup runs in $DEST_DIR" >&2
    exit 1
fi

# The `-wal`/`-shm` sidecars are excluded on purpose. src/tools/backup.js
# writes a self-contained database that needs neither, but merely *opening* a
# backup to check it leaves them in the run directory — and a restore that
# copies a stale `-wal` back alongside the `.db` is a restore that can come
# back wrong. Shipping only the `.db` makes that mistake impossible.
#
# Non-zero exit propagates through ssh, so a truncated transfer fails the
# caller's job rather than leaving a short file that looks fine.
exec tar czf - --exclude='*-wal' --exclude='*-shm' -C "$DEST_DIR" "$RUN"
