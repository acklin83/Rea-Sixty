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

## What is NOT deployed

- The production database starts **empty**. Frank's 29 local maps are not
  published — `src/seed.js` seeds a *development* database from his on-disk
  catalog and reads his REAPER scan caches for vendors. Publishing those is a
  decision, not a deploy step.
- No backup of `/opt/reasixty/var/` is configured yet.
