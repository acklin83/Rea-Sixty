#!/bin/sh
# Nightly backup of the mapping exchange's state. Runs on the VPS host from
# /etc/cron.d/reasixty-backup; see docs/deploy-exchange.md for restore.
#
# One directory per run, so a run's artefacts stay together:
#
#   backups/<stamp>/exchange.db     the database, via SQLite's online backup API
#   backups/<stamp>/maps.tar.gz     the uploaded .rea60map blobs
#
# WHY A DIRECTORY AND NOT TIMESTAMPED FILES. An SQLite file grows `-wal` and
# `-shm` siblings the moment anything opens it — including opening a *backup*
# to check it. A retention rule matching `exchange-*.db` then deletes the
# database and leaves its sidecars behind for ever. Pruning whole directories
# cannot miss a file it did not know to look for.
#
# ORDER MATTERS, and not the way you would guess. The database is snapshotted
# FIRST, the blobs SECOND. A map uploaded between the two steps then leaves a
# blob the database does not reference — a harmless orphan. The other order
# would leave the database referencing a blob that is not in the tar, which is
# a restore that comes back broken.
#
# The database is NOT copied with cp: it runs in WAL mode, so the main file on
# its own is missing recent commits. src/tools/backup.js uses SQLite's online
# backup API instead and writes one self-contained file.

set -eu

APP_DIR=${APP_DIR:-/opt/reasixty}
DEST_DIR=${DEST_DIR:-$APP_DIR/backups}
KEEP_DAYS=${KEEP_DAYS:-14}

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
RUN_DIR=$DEST_DIR/$STAMP

mkdir -p "$RUN_DIR"
# The image runs as uid 1000; it has to be able to write into the run
# directory the host just created.
chown 1000:1000 "$RUN_DIR"
cd "$APP_DIR"

# The container mounts $DEST_DIR at /backups (docker-compose.yml), so the path
# the container writes to and the path pruned below are the same directory.
docker compose exec -T exchange node src/tools/backup.js "/backups/$STAMP/exchange.db"

# -C keeps the archive rooted at `maps/`, so a restore is
# `tar xzf maps.tar.gz -C /opt/reasixty/var`.
tar czf "$RUN_DIR/maps.tar.gz" -C "$APP_DIR/var" maps

# Retention: whole runs, never individual files.
find "$DEST_DIR" -mindepth 1 -maxdepth 1 -type d -mtime "+$KEEP_DAYS" -exec rm -rf {} +

RUNS=$(find "$DEST_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l)
echo "backup $STAMP ok — $RUNS run(s), $(du -sh "$DEST_DIR" | cut -f1) in $DEST_DIR, keeping ${KEEP_DAYS}d"
