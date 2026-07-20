# Session notes — 2026-07-20 · Mapping Exchange, in-app + refinements

Continues `docs/session-2026-07-19-exchange-ui.md` (UI design) and Phase 2
foundation. This session built the **whole in-app exchange** and refined it
against Frank's feedback. Branch `mapping-exchange` (local-only, no upstream).

## What shipped (38 commits since main; server-side JS + the C++ extension)

**Server (`server/`, Node + Fastify + better-sqlite3):**
- Read routes (`4ef0f48`): `GET /v1/plugins` (browse), `/v1/vendors`,
  `/v1/plugins/:slug` (+`/diff`), `/v1/maps/:id` (+`/download`). Binds
  `127.0.0.1:8010`.
- Write surface (`6c97c0f`): `POST /v1/maps`, **device-token auth**
  (`account_tokens`, SHA-256 only; `mktoken` CLI). Browse/download anonymous.
- Duplicate guard (`6795ff5`): identical bindings → 409 (`content_hash`).
- Vendors from REAPER scan caches (`bbe684e`) + alias folding (`3927760`,
  `data/vendor-aliases.json`).
- Parameter coverage (`a121f65`) — see below.
- Sort by name/vendor/maps/coverage + direction; additive name+vendor search
  (`83c67ef`, `eb781ba`).

**Extension (`extension/src/`):**
- Envelope **v2** (`aaa5559`, `original_name`) + **v3** (`a121f65`,
  `functional_params`). Captured live; importer keys on `format`, not `version`.
- `reasixty::http` — async NSURLSession client (`macos_http.mm`,
  **`-fno-objc-arc`, manual retain/release, NOT `__weak`**). Win/Linux = stub.
- `ExchangeView` — Settings→Exchange rail. Three screens: list → plug-in →
  map detail. Browse, download+install (→ `importMapFromString` → upsert),
  upload ("Publish to exchange" in the Share dialog), live search, **custom
  clickable sort headers** (ReaImGui's built-in Sortable never re-sorted —
  own it: `be39bae`).

## Key decisions & traps (READ before touching this)

- **Coverage: TWO ideas, only PARAMETER shown.** Surface coverage (bound
  controls / surface controls) is computed server-side but **not displayed**
  (Frank). Parameter coverage = distinct mapped params / **functional** params.
- **Functional params = filter the junk.** UADx 1176 reports 2094 params, 2080
  MIDI CC. Filter: `param.X.automatable` (REAPER v7.62+) + drop `:wet/:bypass/
  :delta` idents + the `MIDI ` name skip. Denominator rides in envelope v3
  (`functional_params`) because the exported `paramSnapshot` is pruned.
- **UF8 detail = hardware layout.** 8 channel strips as COLUMNS, one fader bank
  at a time (selector when both used — only SSL Delta Control 16 does). Strip
  controls (Fader/Solo/Mute/Sel) first, then V-Pot bank rows with a mode tag
  (turn/step/toggle). NOT an 8×8 grid.
- **STRIP-KEY BUG (fixed):** UF8 strips are NESTED on disk
  (`{"fader":{"vst3Param":N}}`), not flat `faderVst3Param`. Strip coverage read
  0 forever until fixed. Same on-disk-vs-struct trap as `banksByFaderBank`.
- **linkIdx is namespaced PER DOMAIN** (CS 1 = FaderLevel, BC 1 = Threshold).
- **Security rule holds:** maps only, never `.rea60config` bundles (429/422).
- ReaImGui table sort specs (`TableGetColumnSortSpecs`/`TableNeedSort`) did not
  drive a refetch reliably — custom Selectable headers instead.

## Current state / how to run

- Local server: `cd server && node src/seed.js --reset && PORT=8010 node src/server.js`.
  Seed fills vendors from REAPER caches + estimates `functional_params` by name.
- Tests: `cd server && node --test "test/**/*.test.js"` → **57 pass**.
- Extension: built + deployed to `~/Library/.../UserPlugins/reaper_rea-sixty.dylib`.
  macOS only for HTTP; needs REAPER v7.62+ for the automatable filter.
- Device token for uploads: `npm run mktoken -- --account Frank` (paste into
  Exchange → Server settings; old tokens die on `--reset`).

## NOT done — the remaining Phase 2/3/4 work

1. **Deploy to the VPS** — own container `/opt/reasixty/`, Caddy block on host
   `/etc/caddy/Caddyfile` (NOT `/home/frank/Caddyfile`), port 8010, `bind` the
   IPs. DNS `api.reasixty.com` → A `187.77.89.149` + AAAA `2a02:4780:79:6af5::1`
   (Frank does it by hand in Hostinger; currently on parking NS).
2. **Website** (`reasixty.com`) — the passkey/magic-link login that issues
   tokens to normal users. Admin uses `mktoken` for now.
3. **Win/Linux HTTP shims** (WinHTTP / dlopen libcurl) — macOS-only today.
4. Verify the extension's live capture (`functional_params`, `original_name`)
   on a freshly-learned map in REAPER (seed uses offline estimates).
