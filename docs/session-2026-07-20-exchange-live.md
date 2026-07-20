# Session notes — 2026-07-20 · The exchange goes live

Continues `docs/session-2026-07-20-exchange-inapp.md`. That session built the
in-app exchange; this one shipped everything around it: the Win/Linux HTTP
clients, the server on the VPS, backups, accounts and moderation, the website,
and the mail that makes sign-in work.

Branch `mapping-exchange` (pushed). Website on `website` — **local-only, never
pushed**, as always.

## What is live

| | |
| --- | --- |
| `https://reasixty.com` | the site + the exchange, Astro on the Node adapter |
| `https://api.reasixty.com` | the exchange API, Fastify |
| Mail | `info@stoersender-studio.ch` via Hostpoint, `spf/dkim/dmarc=pass` |
| Backups | nightly on the VPS, the Synology pulls over Tailscale |
| DNS | Cloudflare, **DNS-only** — moved off Hostinger, see below |

Production database is **empty on purpose**. Seeding it would publish Frank's
29 personal maps; that is a decision, not a deploy step.

## Built

**Cross-platform HTTP** (`bea8804`, `566ceea`) — the in-app exchange left macOS.
Windows = WinHTTP; Linux = libcurl **dlopen'd, not linked**, because linking
would make the whole `.so` fail to load without the matching soname and would
make libcurl-dev a build requirement for source builds. CI-green on all four
jobs; **never RUN on Windows or Linux**.

**Accounts, sign-in, moderation** (`35a41c7`, `7ce2d31`) — passkey and magic
link as EQUAL credentials (Linux has no OS passkey store and Linux ships), 30-day
sessions, an account page that issues the device tokens REAPER uploads with, web
upload, a report queue, suspension, merge tools.

**The website** (`547fb67`, `ee10355`, `fa021e5` on `website`) — `/mappings` and
its two detail screens, `/login`, `/welcome`, `/account`, `/upload`, `/admin`.
Marketing pages stay static; only these render on demand. Placeholder logo
removed, donation link wired to the extension's own URL.

**Deploy + backups** (`1ad3c49`, `0eff087`, `810aa0e`, `46a44b3`) — two
containers, Caddy blocks, nightly database + blob backup, off-box pull.

## The decisions that are forced, not chosen

**Same-origin for the website.** A passkey is bound to the RP ID and the browser
refuses a cross-origin WebAuthn call *before* it reaches any server. So the site
proxies `/auth` and `/v1` onto its own origin. Consequence: Caddy needs no path
rules — `reasixty.com` goes to the Node server, full stop.

**`vite.server.proxy` does not work** for that. Astro's dev server answers
unmatched paths with its own 404 before Vite's middleware sees them — measured
as `text/html` 404 through the site and 200 straight at the API. Real Astro
routes instead (`src/pages/auth/[...path].ts`).

**The site reaches the API by service name on a shared docker network**, not
through the host. The API publishes on the host's `127.0.0.1:8010`, and a
container's loopback is its own — the bridge-gateway address reaches the host
but nothing is listening there. The failure was polite: every exchange page
rendered "the exchange is not reachable" with a 200.

**Mail from `stoersender-studio.ch`, not `reasixty.com`.** That mailbox exists
and its SPF already redirects to Hostpoint, so mail is aligned on the first
send. A fresh sender would need SPF, DKIM and reputation first — and a sign-in
link in a spam folder is an account nobody creates.

## Traps, all of them measured

- **The server refuses to start without SMTP** unless `MAIL_DEV_LOG=1`. Without
  credentials the transport logs the link and reports success — a silent outage
  of the only way in.
- **`requireTLS` on port 587.** nodemailer otherwise treats STARTTLS as
  best-effort and continues in cleartext if the upgrade fails, with `AUTH PLAIN`
  — the mailbox password — as the next thing on the wire.
- **EHLO announced `[127.0.0.1]`** until `SMTP_HELO` was set. Visible in the
  first delivered message's Received header. An IP literal is a textbook spam
  signal under a `p=quarantine` policy.
- **`env_file:` is not the `.env` Compose auto-reads.** The automatic one only
  substitutes `${VARS}` inside the compose file. And a bare `- .env` makes every
  compose command fail when the file is absent — including the `docker compose
  exec` the nightly backup runs. `required: false`.
- **`docker compose up -d` reuses the old image.** Cost three separate
  debugging detours; `--build` whenever a file under `src/` changed.
- **`cp` is not a backup of a WAL database**, a restore must not happen under a
  running server, and a count read from a second process while the server runs
  is not the truth. All three hit in one session — see the memory note.
- **`maps.file_path` is relative to `MAPS_DIR`,** not `DATA_DIR`; the schema
  comment said otherwise and a cleanup tool believed it, reporting "0 files
  removed" while leaving 29 orphans.
- **An adapter moves the build output** to `dist/client` + `dist/server`;
  `dist/index.html` stops existing and the build verifier caught it.
- **Astro's table header was never sticky** — `.scroll-x` becomes the containing
  block for `position: sticky`. Measured at `top: -438px` after scrolling.

## The DNS story, because it will come up again

Hostinger's parking nameservers **kept serving a deleted A record** for over an
hour, inconsistently between and within nameservers, and only half-applied a TTL
change. Effect: roughly every second visitor got Hostinger's parked-domain page,
which answers **200 on every path** — so a smoke test through the normal
hostname passed while proving nothing.

The tell is the TLS certificate date: the parking cert is dated the day the
domain was registered, ours the day Caddy issued it. Pin the request to be sure:

```sh
curl -sI --resolve reasixty.com:443:187.77.89.149 https://reasixty.com/
```

Zone moved to Cloudflare, **DNS-only (grey cloud)**. Orange would make
Cloudflare terminate TLS, and Caddy's TLS-ALPN renewal would then never reach
the box — failing ~60 days later, silently.

## Added after the first write-up

**Linking a machine** (`dcd4a35`) — the normal way to get an upload token now.
REAPER shows a short code, opens the browser, you confirm once, and the token
arrives in the extension by itself. RFC 8628's shape, for its reason: the
device cannot run a browser. Emailing the token was refused — it is a
credential, and it would sit in a mailbox and every backup of it.

The token is minted at COLLECTION, not at approval, so nothing plaintext ever
rests in a row waiting to be picked up and the approving browser never sees it.

Every device flow is phishable: somebody starts a grant, sends you the code,
your approval hands them a token on your account. The `/link` page is the whole
defence — it names the machine, states what is granted in the second person,
and never approves from a link alone. Do not add a one-click auto-approve.

That opened a hole which is now closed: `/link` bounces anonymous visitors to
`/login?next=…`, so `next` had to exist, and an unchecked one is an open
redirect travelling in a mailed link. Same-site paths only, checked at both
ends.

**"Works for me" in the extension** (`ea59526`) — the API always accepted either
credential for it; nothing in REAPER used it. `worksMine` is a tristate and
`null` is not `false`; wdl_json has no `is_null()`, so a JSON null reads back as
the literal string `"null"`.

**No coverage bars** (`f25946c`, Frank) — the figure alone. The plug-in index
keeps a percentage because that row is a plug-in and `x/y` belongs to a map.

87 server tests.

## Still open

1. ~~Frank is not a moderator on production~~ — done. Note for next time: the
   tools live in the image, so it is
   `docker compose exec -T exchange node src/tools/mkadmin.js --email …`.
   Running it on the host gives `Cannot find package 'better-sqlite3'`. And
   `mktoken` CREATES an account when the name is unknown, marked admin — that
   is how a login-less admin row appeared on production and had to be deleted.
2. **No mappings published yet** — the corpus is empty by design.
3. **The `website` branch has no backup.** It exists on one machine. A private
   second repo or a copy on the NAS would both work; not decided.
4. **Win/Linux HTTP clients compile but have never run** on those platforms.
5. Live capture of `functional_params` / `original_name` on a freshly-learned
   map in REAPER (the seed used offline estimates).
