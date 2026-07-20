# Deploying the mapping exchange (api.reasixty.com)

The exchange server (`server/`) runs as its own container on the Hostinger VPS
`srv1401714.hstgr.cloud` (`187.77.89.149` / `2a02:4780:79:6af5::1`), behind the
Caddy that already fronts brandy, Drums de Lion and the two Listmonks.

First deployed 2026-07-20.

## The three facts that decide the shape

1. **Caddy runs on the HOST, not in Docker** (`/usr/local/bin/caddy`, systemd).
   So a new service is *a container publishing to loopback* + *a `reverse_proxy`
   block in the host Caddyfile*. Never a docker-network attach.
2. **`/etc/caddy/Caddyfile` is the live one.** `/home/frank/Caddyfile` also
   exists, is shorter, and is **not** loaded — appending to it does nothing and
   looks like it worked. Confirm with
   `systemctl cat caddy | grep ExecStart`.
3. **Every block binds the IPs explicitly.** Omit `bind` and the new site does
   not answer. Copy the pattern from a neighbouring block.

## Layout on the box

```
/opt/reasixty/
├── Dockerfile            synced from server/
├── docker-compose.yml    synced from server/
├── src/ data/ …          synced from server/
└── var/                  THE ONLY STATE — exchange.db + uploaded .rea60map
                          blobs. Owned by uid 1000 (the image runs as `node`).
                          Not synced, not in git.
```

The container publishes `127.0.0.1:8010:8010` — the newer-service pattern on
this box (ddl-website, listmonk), unlike the older ones that still expose
`0.0.0.0`. Port 8010 was free and is now taken.

Note `data/` vs `var/`: `data/` is **committed config** (the generated
`uc1-controls.json` and `vendor-aliases.json`, baked into the image at
`/app/data`, resolved relative to `src/lib/`). `var/` is **runtime state**,
mounted at `/data` via `DATA_DIR`. Different paths, do not merge them.

## Deploy / redeploy

From a checkout on the Mac:

```sh
rsync -az --delete \
  --exclude 'node_modules/' --exclude 'var/' --exclude '.env*' \
  server/ root@srv1401714.hstgr.cloud:/opt/reasixty/

ssh root@srv1401714.hstgr.cloud 'cd /opt/reasixty && docker compose up -d --build'
```

`--exclude 'var/'` is not optional: without it a local `--reset` database
overwrites production.

Verify on the box (Caddy is not involved yet):

```sh
curl -s http://127.0.0.1:8010/health          # {"ok":true,…}
curl -s "http://127.0.0.1:8010/v1/plugins?pageSize=3"
cd /opt/reasixty && docker compose ps          # STATUS should say (healthy)
```

## Caddy block

Appended to `/etc/caddy/Caddyfile` (backup kept as
`Caddyfile.bak-20260720-reasixty`):

```
api.reasixty.com {
	bind 187.77.89.149 2a02:4780:79:6af5::1
	reverse_proxy localhost:8010
}
```

Then `caddy validate --config /etc/caddy/Caddyfile` and
`systemctl reload caddy`. Validate first — a reload with a bad file keeps the
old config, but a *restart* would take every site down.

## DNS — the one step that is not automatable

`reasixty.com` sits on Hostinger **parking** nameservers
(`solar/lunar.dns-parking.com`), apex → `2.57.91.91`, which is a parking IP and
**not** this VPS. `api.reasixty.com` does not resolve at all.

Two records are needed, in Hostinger's DNS zone editor:

| Type | Name  | Value                     | TTL     |
| ---- | ----- | ------------------------- | ------- |
| A    | `api` | `187.77.89.149`           | default |
| AAAA | `api` | `2a02:4780:79:6af5::1`    | default |

Until they exist, Caddy logs
`NXDOMAIN looking up A for api.reasixty.com` and retries — `retrying_in: 60`,
for up to 30 days. **Nothing else needs doing after the records land**; the
certificate is issued on the next retry. Force it with
`systemctl reload caddy` if you don't want to wait.

This step needs the Hostinger panel password, so it is Frank's to do — Claude
does not enter credentials anywhere, authorised or not.

## Upload tokens

Browse and download are anonymous. Uploading needs a device token:

```sh
ssh root@srv1401714.hstgr.cloud \
  'cd /opt/reasixty && docker compose exec exchange npm run mktoken -- --account Frank'
```

The token is printed once and stored only as a SHA-256 hash. Paste it into
REAPER: Settings → Exchange → Server settings, together with the URL
`https://api.reasixty.com`.

The website that issues tokens to normal users (passkey / magic-link) is not
built yet; `mktoken` is the admin path.

## Backups

Two layers. The on-box one covers mistakes (a bad deploy, an accidental
`seed.js --reset`); the off-box one covers losing the VPS.

### On the VPS — nightly, 03:17 UTC

`/etc/cron.d/reasixty-backup` runs `/opt/reasixty/backup.sh`, logging one line
per run to `/var/log/reasixty-backup.log`. Output:

```
/opt/reasixty/backups/<stamp>/exchange.db     online snapshot of the database
/opt/reasixty/backups/<stamp>/maps.tar.gz     the uploaded .rea60map blobs
```

Three things about that script are load-bearing:

- **The database is never `cp`'d.** It runs in WAL mode, so the main file on
  its own is missing recent commits. `src/tools/backup.js` uses SQLite's online
  backup API (`db.backup()`), which reads a consistent image of a database that
  is being written to and writes one self-contained file.
- **Database first, blobs second.** A map uploaded between the two steps leaves
  a blob the database does not reference — a harmless orphan. The other order
  leaves the database referencing a blob that is not in the archive, which is a
  restore that comes back broken.
- **Retention prunes whole directories, never files.** An SQLite file grows
  `-wal`/`-shm` siblings the moment anything opens it, including opening a
  backup to verify it. A rule matching `exchange-*.db` deletes the database and
  leaves the sidecars for ever.

### Off the VPS — the NAS pulls, the VPS never pushes

`nas-pull-reasixty.sh` lives on the Synology at
`/volume1/homes/Frank/scripts/nas-pull-reasixty.sh` and drops archives into
`/volume1/Franks Cloud/Backups/reasixty/`, 90 days. (There is no
`/volume1/backup` share on that box — check `ls -d /volume1/*/` before
inventing a path.)

Pull, not push, on purpose: the VPS holds no credential that can write to the
NAS, so taking the VPS does not get you the backups. The key is locked to a
forced command on the VPS side:

```
# /var/lib/reasixty-backup/.ssh/authorized_keys
command="/usr/local/bin/reasixty-backup-fetch",restrict ssh-ed25519 AAAA…
```

Verified on the box, not assumed: the default invocation returns the tar,
`list` prints run names, any other command exits 2, and `scp` is refused
outright (255).

A tar stream rather than rsync or scp because the far end needs nothing but an
ssh client — and the Synology's rsync has already refused to cooperate once in
this setup.

#### ⚠ Use the public hostname, never the tailnet IP

The obvious address for this is the VPS's Tailscale IP. It does not work, and
it fails in the worst way — *quietly, with the security gone*.

The VPS runs **Tailscale SSH** (`tailscale debug prefs` → `"RunSSH": true`).
Tailscaled intercepts port 22 on the tailnet address and authenticates by
tailnet identity: the connection never reaches the system sshd, so
`authorized_keys` — and the forced command with it — is never consulted. What
you get is a full login shell as `reasixty-backup`, and `ssh -v` says
`Authentication succeeded (none)` while the VPS's auth log stays empty, because
sshd was never involved. The key is not used at all.

For this script that is not only a lost restriction, it is a break: with no
forced command, `ssh … > file` opens a login shell instead of writing a tar.

So the script talks to `srv1401714.hstgr.cloud`. The system sshd is then back
in the path and the restriction holds — verified from the NAS: `list` returns
run names, `cat /etc/shadow` is refused. Port 22 is publicly reachable either
way, and the account is *not* reachable from the internet without the key
(`Permission denied (publickey,password)`; the password is locked, `!`).

#### State

Done: the key is on the NAS at `/volume1/homes/Frank/.ssh/reasixty_nas_key`
(600), the VPS's copy is shredded, the script is in place, and a real pull has
run — the archive on the NAS is byte-identical to the source
(`sha256sum` matched on both ends).

Remaining: the **DSM scheduled task**. Control Panel → Task Scheduler → Create
→ Scheduled Task → User-defined script; user `root`, daily ~04:00 (after the
VPS's 03:17 UTC run), command:

```sh
/bin/sh /volume1/homes/Frank/scripts/nas-pull-reasixty.sh
```

Check by hand any time with
`ssh -i /volume1/homes/Frank/.ssh/reasixty_nas_key reasixty-backup@srv1401714.hstgr.cloud list`.

### Restore

```sh
cd /opt/reasixty
docker compose down
tar xzf <run>.tar.gz                       # or take it from backups/<stamp>/
cp <stamp>/exchange.db var/exchange.db
rm -f var/exchange.db-wal var/exchange.db-shm   # stale sidecars beat the restore
tar xzf <stamp>/maps.tar.gz -C var
chown -R 1000:1000 var
docker compose up -d
```

Verify before believing it:

```sh
docker compose exec -T exchange node -e "
const D=require('better-sqlite3');const d=new D('/data/exchange.db',{readonly:true});
console.log(d.pragma('integrity_check',{simple:true}),
            d.prepare(\"SELECT count(*) c FROM sqlite_master WHERE type='table'\").get().c);"
```
Expect `ok 14`.

**Never `rm -rf` a bind-mounted directory** (`var/`, `backups/`) while the
container runs — that replaces the inode and the container keeps writing to the
deleted one, which shows up as `Cannot save backup because the directory does
not exist`. `docker compose up -d --force-recreate` re-binds it.

## What is NOT deployed

- The production database starts **empty**. Frank's 29 local maps are not
  published — `src/seed.js` seeds a *development* database from his on-disk
  catalog and reads his REAPER scan caches for vendors. Publishing those is a
  decision, not a deploy step.
