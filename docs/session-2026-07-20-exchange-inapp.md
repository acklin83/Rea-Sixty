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

## Win/Linux HTTP clients (later the same day)

`reasixty::http` is no longer macOS-only. Same interface, one implementation
per platform, each self-guarded so the others compile to nothing:

| Platform | File | Mechanism |
| --- | --- | --- |
| macOS | `macos_http.mm` | NSURLSession, `-fno-objc-arc` |
| Windows | `win_http.cpp` | WinHTTP (`winhttp.lib`, ships with the OS) |
| Linux | `linux_http.cpp` | libcurl **dlopen'd**, not linked |
| other | `HttpClient.cpp` | stub returning a clear error |

- **Linux dlopens libcurl on purpose.** Linking it would make the whole
  `reaper_rea-sixty.so` fail to load on a machine without the matching soname
  — killing UF8/UC1 support over a feature most users never touch — and would
  make `libcurl4-openssl-dev` a build requirement for everyone building from
  source. Missing libcurl now degrades to a readable error. Consequence: no
  curl headers at build time, so the `CURLOPT_*` ids are spelled out in the
  file. They were read out of a real `curl.h`, and curl never renumbers an
  option.
- **Both use a thread per request** running the synchronous native calls and
  dropping the `Response` into a mutex-guarded map that `poll()` drains on the
  main thread — the same fire-and-poll shape as macOS, so the UI never blocks.
  `cancel()` detaches: neither `curl_easy_perform` nor a synchronous WinHTTP
  call can be interrupted from outside, so the request runs to its timeout and
  nobody waits for it.
- **CI is the only verification.** All four jobs green on `566ceea`
  (linux-x86_64, windows-x64, macos-arm64, macos-x86_64). That is a *compile*
  guarantee — **nobody has run the exchange on Windows or Linux yet.**

### Two cross-platform breaks CI found in the existing exchange UI

Neither was in the new HTTP files; both were in code written and tested on
macOS only, in a repo whose workflow builds nothing but `main`.

- **MSVC:** `ExchangeView.cpp` is the only TU that includes `WDL/jsonparse.h`,
  which pulls `wdlcstring.h`, which does `#define snprintf WDL_snprintf` on
  MSVC. Every `std::snprintf` after that include expanded to
  `std::WDL_snprintf` — 21 hard errors. The shim is now `#undef`'d in that TU
  (MSVC 2022's `std::snprintf` is conforming). Same family as the
  `std::min`/`std::max` NOMINMAX trap: a Windows-side macro poisoning a
  `std::`-qualified name.
- **GCC:** `mapShare = {}` is rejected ("no known conversion from
  `<brace-enclosed initializer list>`") where Clang accepts it. Four sites,
  all resetting a pending `MapShare`. Now an explicit `MapShare()` temporary,
  which both accept.

## Current state / how to run

- Local server: `cd server && node src/seed.js --reset && PORT=8010 node src/server.js`.
  Seed fills vendors from REAPER caches + estimates `functional_params` by name.
- Tests: `cd server && node --test "test/**/*.test.js"` → **57 pass**.
- Extension: built + deployed to `~/Library/.../UserPlugins/reaper_rea-sixty.dylib`.
  HTTP now on all three platforms (see above); needs REAPER v7.62+ for the
  automatable filter.
- Device token for uploads: `npm run mktoken -- --account Frank` (paste into
  Exchange → Server settings; old tokens die on `--reset`).

## NOT done — the remaining Phase 2/3/4 work

1. ~~Deploy to the VPS~~ — **done**, container live and healthy on
   `127.0.0.1:8010`, Caddy block in `/etc/caddy/Caddyfile`. Runbook:
   `docs/deploy-exchange.md`. **Still blocked on DNS:** `api.reasixty.com`
   needs A `187.77.89.149` + AAAA `2a02:4780:79:6af5::1` in Hostinger (still
   on parking NS). Caddy retries ACME every 60 s for 30 days, so the cert
   appears on its own once the records land. Production DB is empty on
   purpose. No backup of `/opt/reasixty/var/` yet.
2. **Website** (`reasixty.com`) — the passkey/magic-link login that issues
   tokens to normal users. Admin uses `mktoken` for now.
3. ~~Win/Linux HTTP shims~~ — **built, and CI-green on all four jobs.** Still
   needs a *runtime* test on an actual Windows and Linux box.
4. Verify the extension's live capture (`functional_params`, `original_name`)
   on a freshly-learned map in REAPER (seed uses offline estimates).
