# Mapping Exchange — plan

A place where users publish the plugin mappings they built and pick up mappings
built by others. Sorted by vendor, rated, attributed to an author, with an
optional short description.

Status: planned, 2026-07-19. Nothing implemented yet.

## The decision: server + website first, in-app browser second

This is not a choice between "in Settings" and "a website". The backend —
database, vendor facets, ratings, authorship — has to exist either way. The only
open question was which frontend comes first, and that is settled by two
technical facts:

- **The extension has no HTTPS stack.** No TLS, no DNS resolution, no HTTP
  library. The only HTTP code in the tree is a hand-written HTTP/1.0 request to
  a fixed IP for DynaMount mic stands (`DynaMountClient.cpp:54`). In-app
  download has to be built before it can be used.
- **Upload is form-heavy.** Description, vendor picker, licence checkbox, login.
  That is cheap in HTML and unpleasant in ImGui.

So the website ships first. It is not a stepping stone — it stays the permanent
upload, moderation and rating surface. The later in-app browser consumes the
same API.

## What the codebase already gives us

**The surface taxonomy already exists in the data.** UC1 / UF8 / UC1+UF8 is
exactly the `(domain, uf8Mode)` pair documented at
`UserPluginCatalog.h:492-499`:

| `domain`                 | `uf8Mode` | scope       |
| ------------------------ | --------- | ----------- |
| `ChannelStrip` / `BusComp` | `false`   | UC1 only    |
| `ChannelStrip` / `BusComp` | `true`    | UC1 + UF8   |
| `None`                   | `true`    | UF8 only    |
| `None`                   | `false`   | invalid — filtered at load/save |

No new field is needed; the server derives and indexes it.

**A plugin map is pure data.** Param indices, labels, curves, colours, push
steps. `ExtFuncEntry` is `{name, vst3Param}` (`UserPluginCatalog.h:507`) — a
label and a param index, not an action. There are no REAPER action IDs and no
keyboard macros anywhere in `UserPluginMap`.

**Setup bundles are not pure data.** `.rea60config` embeds `bindings.json`
(`SetupBundle.cpp:196-216`), which *can* carry keyboard macros and REAPER
action IDs. Importing one executes whatever the author put in it.

> **Rule: the platform hosts single plugin maps only. Never `.rea60config`
> bundles.** Deciding this now costs nothing; retrofitting it after launch is
> expensive.

**Vendor does not exist anywhere.** A plugin is identified by a case-sensitive
*substring* of the FX identity name (`PluginMap.cpp:426-447`, `fxIdentityName`
→ `original_name`, falling back to `TrackFX_GetFXName`). Real examples from a
live catalog: `"VST3: bx_console SSL 4000 G"`, `"JS: 4-Band EQ
[loser/4BandEQ]"`. No manufacturer in either. Vendor has to be captured at
upload time from a server-side list with alias normalisation, or the corpus
fragments into "FabFilter" / "Fabfilter" / "FF".

> **Open, unverified:** whether REAPER's full `TrackFX_GetFXName` output carries
> the manufacturer for VST3/AU/CLAP. Check against real plugins before relying
> on auto-extraction. Do not assume.

**Export is whole-catalog only.** `exportToFile()` writes every map. A live
catalog on the dev machine is 706 KB with 29 maps. Sharing one map needs a new
path — required by the website route and the in-app route alike, which is why
it is Phase 1.

**`paramSnapshot` is unbounded.** One object per plugin parameter, no sparse
path. Two UADx entries alone account for 411 KB of that 706 KB file. On export
it gets pruned to the params actually referenced by a slot — 2094 entries
collapse to roughly 30.

### Size reference

| Class                             | Bytes            |
| --------------------------------- | ---------------- |
| Small CS/BC map                   | ~1.3 KB          |
| Typical CS/BC map (median)        | ~3.5 KB          |
| Large CS/BC map (27 slots)        | ~7.2 KB          |
| UF8-mode map (always 2×8×8 cells) | ~22–35 KB        |
| Pathological (`paramSnapshot`)    | up to ~195 KB    |

The entire corpus stays in the low single-digit megabytes for a long time.
SQLite plus files on disk is the right size of tool.

## Settled decisions

| Question | Decision |
| --- | --- |
| **Auth** | Magic-link email. Upload and rating require login; browsing and downloading are anonymous. Kills most spam for free. SMTP already exists (Hostpoint, `asmtp.mail.hostpoint.ch:587`, from `info@stoersender-studio.ch`). |
| **Rating** | "Works for me" count plus confirmed-plugin-version, not 5 stars. With few votes per item a star average is noise; a version confirmation is the signal that actually helps the next person. |
| **Licence** | CC0 checkbox at upload, so mappings can be redistributed. Cannot be collected retroactively. |
| **Scope** | Single plugin maps only. No setup bundles. See the rule above. |
| **Domain** | `reasixty.com` (verified unregistered 2026-07-19), API at `api.reasixty.com` — matches the existing `api.brandy-barf.app` pattern. |

## Phases

### Phase 1 — `.rea60map`, one file for one map

Extension only, no networking. Useful on its own: mappings become shareable by
file, over forums or Discord, before any server exists.

- New envelope format: metadata (plugin, vendor, author, description, surface
  scope, id, revision, licence) plus exactly one map.
- Per-row **Export…** in the existing plugin-map list.
- **Import…** via file dialog, with collision handling — `upsert()`,
  `SaveResult::Collision` and `collidesWithBuiltin()` already exist.
- Prune `paramSnapshot` to referenced params on export.

### Phase 2 — reasixty.com + api.reasixty.com

Server. Runs on the existing Hostinger VPS; Docker and Caddy are already there.

- SQLite plus files on disk.
- Upload (login required), browse by vendor / surface / plugin, rate, author,
  description.
- Download serves `.rea60map`; the user imports it via Phase 1.
- Admin view with unpublish, plus a report button. Not optional — one bad actor
  otherwise poisons the corpus.
- **Seed with the 29 maps from the dev machine.** An empty platform reads as a
  dead one; this one launches with content.

### Phase 3 — in-app browser (read-only)

- HTTP shim behind one interface: `NSURLSession` on macOS (Objective-C++ is
  already in the build — `macos_open_dialog.mm`, `macos_save_dialog.mm`,
  `macos_pin_fx_gui.mm`), WinHTTP on Windows (no redistributable), libcurl via
  `dlopen` on Linux. **No new bundled binary → no notarisation or ReaPack
  packaging fallout.**
- New rail section: three mechanical edits (`Section` enum + `kRail` row in
  `MixerWindow.cpp:44/65`, draw function). It can live in its own file rather
  than growing the 18k-line `SettingsScreen.cpp` — `ManualView` is the
  precedent (`MixerWindow.cpp` rail accepts any `void(ImGui_Context*)`).
- List, filter by vendor and surface, search, install.
- The feature that actually matters: **"3 mappings available for this plugin"**
  surfaced the moment an unmapped plugin is focused.

### Phase 4 — in-app upload

"Share this mapping" POSTs a draft, then `reasixty_openUrl()`
(`main.cpp:26559`, cross-platform, already used) opens the page to fill in the
description. The data leaves the app; the metadata is entered in the browser.

## Notes

- Distribution of the extension itself stays as it is: GitHub release plus the
  ReaPack index in `acklin83/reaper-scripts`. The mapping exchange is a separate
  system and deliberately not a ReaPack repository — ReaPack has no ratings, no
  vendor facets, and one repo per author would be absurd.
- `docs/user-manual.md:931` claims catalog schema v7 while the code is at v10
  (`UserPluginCatalog.h:585`). Unrelated drift, worth fixing when the manual is
  next touched.
