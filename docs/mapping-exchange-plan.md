# Mapping Exchange — plan

A place where users publish the plugin mappings they built and pick up mappings
built by others. Sorted by vendor, rated, attributed to an author, with an
optional short description.

Status: Phase 1 **shipped** 2026-07-19 (`2befc2f`). Phases 2–4 planned. The
visual preview (see below) is the piece that decides whether the detail page is
a real service or a file dump.

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

**Vendor — RESOLVED 2026-07-19. It comes from REAPER, inside the name string.**

There is **no vendor field in the API.** `TrackFX_GetNamedConfigParm` documents
exactly five keys — `fx_ident`, `fx_name`, `fx_type`, `original_name`,
`renamed_name` (checked in `reaper_plugin_functions.h`). None of them is a
manufacturer.

But REAPER's own plug-in scan caches carry the vendor in the display name, and
`original_name` — which the extension **already reads**
(`PluginMap.cpp:426-447`) — is that string. Measured against the caches on the
dev machine:

| Format | Cache | Convention | Entries | Vendor found |
| --- | --- | --- | ---: | ---: |
| VST / VST3 | `reaper-vstplugins_arm64.ini` | `Name (Vendor)` | 635 | **99 %** |
| AU | `reaper-auplugins_arm64.ini` | `Vendor: Name` | 941 | **100 %** |
| CLAP | `reaper-clap-macos-aarch64.ini` | `Name (Vendor)` | 14 | **100 %** |
| JSFX | `reaper-jsfx.ini` | `JS: Name (Vendor) [path]` | 227 | **8 %** |
| | | | **1817** | **88 %** |

**Three different conventions, so the parser is format-scoped** — read `fx_type`
(or the `VST:`/`AU:`/`JS:`/`CLAP:` prefix) first, then apply the matching rule.
A trailing parenthetical is not always the vendor: channel specs like `(2->6ch)`,
`(32 out)` and the `!!!VSTi` suffix appear in the same position and must be
skipped. JSFX is the weak case at 8 % — community scripts mostly carry no
vendor — so JS maps will need the uploader to supply one.

**Do NOT parse `match`.** That is a user-editable substring, often shortened by
hand: only 8 of the 29 maps in the live catalog carry a recoverable vendor, and
one of those yields `"mono"` from `"… (SSL) (mono)"`. Capture the vendor at
**export** time from the live `original_name`, and store it in the envelope —
which is what the shipped `.rea60map` already has a field for.

**Alias normalisation is not hypothetical.** The same machine produces both
`Universal Audio (UADx)` (72 plug-ins) and `UADx` (70) as separate vendors. The
server needs an alias table from day one, exactly as this plan already assumed.

**82 distinct vendors on one machine, over 1817 plug-ins.** A real corpus will
carry several hundred. So the vendor facet is a **searchable combobox with
type-ahead, multi-select chips and per-vendor counts** — a checkbox rail cannot
hold 82 rows, let alone 400.

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
| **Auth** | **Passkey (WebAuthn) primary; magic-link email as an equal-standing fallback; optional email for recovery.** Upload and rating require login; browsing and downloading are anonymous. Revised 2026-07-19 — see below. SMTP already exists (Hostpoint, `asmtp.mail.hostpoint.ch:587`, from `info@stoersender-studio.ch`). |
| **Rating** | "Works for me" count plus confirmed-plugin-version, not 5 stars. With few votes per item a star average is noise; a version confirmation is the signal that actually helps the next person. |
| **Licence** | CC0 checkbox at upload, so mappings can be redistributed. Cannot be collected retroactively. |
| **Scope** | Single plugin maps only. No setup bundles. See the rule above. |
| **Domain** | `reasixty.com` (verified unregistered 2026-07-19), API at `api.reasixty.com` — matches the existing `api.brandy-barf.app` pattern. |

## Auth — why passkeys, and why email did not go away

Revised 2026-07-19, replacing "magic-link email" from earlier the same day.

**One account, several credentials.** An account is a display name plus zero or
more credentials; credential types are *passkey* and *verified email*. This is
deliberately not two auth systems — a user can add a passkey later and drop the
email, or the reverse.

**Why passkeys lead.** WebAuthn is a browser API: no external host, no third
party, nothing loaded from anyone else's server. That matters here more than
usual, because the site's own claim is that it contacts nobody — a "Sign in
with Google" button would contradict the page it sits on, and the build's
external-host check exists precisely to keep that honest. Passkeys also mean
most accounts carry **no personal data at all**: a public key and a chosen
name.

**Why OAuth is rejected outright.** Google/Apple/GitHub sign-in sends the user
to a third party, hands back an email address anyway, and adds a dependency the
rest of the project does not have. It is worse on privacy *and* on
independence. GitHub would fit the audience, but it excludes everyone without a
GitHub account.

**Why email still has to exist.** Passkeys are not universal, and the gap is
not hypothetical here: macOS and Windows have an OS passkey store, **Linux
generally does not**. A Linux user on Firefox typically has no platform
authenticator and needs cross-device (phone + QR), a hardware key, or a
password manager. Linux is one of the three platforms this project ships
binaries for, so a passkey-only login would put a supported platform behind an
extra device. Magic-link is therefore a **first-class** path, not a degraded
one.

**Honest accounting of the privacy win.** Once optional recovery email exists,
the privacy policy has to describe email processing regardless, so magic-link
as a fallback costs nothing further in legal surface. The gain is narrower than
"no PII": it is *most users leave none*. Still worth having — just not for the
stronger reason.

**Consequences to build:**

- The display name is user-chosen and is what appears as the author. The email
  is never shown publicly and never used for anything but sign-in and recovery.
- Account recovery without an email is impossible by construction. Say so at
  signup, next to the "add a recovery email" offer, rather than in a FAQ.
- Rate-limit uploads per account and keep the report/unpublish queue. Neither
  credential type is a real spam wall on its own — throwaway addresses and
  software authenticators are both cheap. The moderation queue is the actual
  defence; auth just raises the floor.
- The privacy policy must be live **before** the first sign-up. `/legal` on the
  site already carries the forward-looking version.

## How comparable exchanges actually do it

Researched 2026-07-19, after a first draft of this section specified a surface
render for every mapping — which is wrong, and obviously so once you look at a
real one.

### reWASD — the closest analogue there is

Community sharing of **hardware control mappings**: arbitrary user configs
against a fixed physical device. Same problem shape as this.

**Browse is a single-column list of rows with no preview image at all.** A row
carries: star rating with vote count, the title (which is the description the
author typed at upload), `by <author>`, a **"Perfect for:"** row of
colour-coded hardware badges, a **"May be used on:"** list of secondary
compatible hardware, and a download button. That is the whole row. No
thumbnail, no render, no miniature of the device.

**Filters are capability checkboxes, not categories** — `hardware mapping`,
`paddles`, `shifts`, `gyro`, `led`, `turbo`, `key combo`, `custom stick
deadzone`… i.e. *what the config uses*, derived from the config itself. Sort is
Best / Latest.

**The visual lives on the detail page only, and it is laid out the opposite way
to my first draft.** Left column: title, author, date, stars, one-line
description, hardware badges, two CTAs. Right column: a render of the device
with **leader lines running outward to labels set in the margin** — the labels
are *not* stacked on the controls. Below it, **carousel dots for several views**
rather than everything at once, and the capability tags repeated as links back
into the browse facets.

That margin-and-leader-line layout is the fix for exactly the collision the
prototype hit. It is a solved problem, and the solution is not "smaller text".

### Elgato Marketplace — a card grid, but not a model to copy

Stream Deck profiles are a 4-up grid **with** thumbnails: title, `by author ·
category`, one-line description, price. But those thumbnails are **marketing
artwork the author made by hand**, not generated from the profile. Filters are
dropdown pills with removable active chips; sort is Popular.

Worth naming because it is the tempting wrong turn: requiring an image from the
uploader is a non-starter here. Someone sharing a Pro-Q mapping will not draw a
banner, and a corpus where most entries have a blank thumbnail looks deader
than one with no thumbnails at all.

## The faceplate is dropped. Decided 2026-07-19 on mobile.

Four prototypes went into a rendered UC1 face — from the hit-test table, then
from `drawUc1Face_`; with stacked labels, then leader lines, then labels on the
control. The last one looked right on a desktop. It does not survive a phone,
and the arithmetic is not close:

| Viewport | Face renders at | 9 px silkscreen becomes |
| --- | --- | --- |
| 980 px (tablet) | 633 px | **6.6 px** |
| 390 px (phone) | ~342 px | **3.6 px** |

The face is an 860 × 660 drawing. There is no label strategy that fixes an
unreadable font size, and a horizontally-scrolling faceplate on a phone is worse
than no picture. Shrinking the drawing is not an option either — it is a
transcription of the hardware, and the parts that make it legible as a UC1 (the
silkscreen, the band labels, the meter scale) are exactly the parts that vanish
first.

**So there is no faceplate anywhere in the exchange.** Not on the index, not on
the plug-in page, not on the detail page.

### What carries the picture instead

1. **The coverage strip** — one small cell per control, grouped by section, lit
   where bound. ~200 px, scales down without losing meaning because it has no
   text in it. Lives on the map rows and on the detail page.
2. **The section-grouped control table** — Filters · HF · HMF · EQ · LMF · LF ·
   Comp · Gate · Channel, each with its own `n/total`. This is the detail view
   now. It reflows to any width, it is what a screen reader and a search engine
   read, and the per-section counts carry the shape of a mapping ("all of the
   EQ, none of the gate") in a way the drawing only ever implied.
3. **The coverage number**, domain-scoped, for everything above that.

Verified at 390 px across all screens: no horizontal overflow, smallest rendered
font 10.6 px — against 3.6 px for the faceplate it replaced.

### Keep for the app, not the web

None of this argues against `drawUc1Face_` itself; it is the right picture *in
the extension*, at desktop size, where it already lives. The Phase 2a exporter is
therefore **dropped from the exchange's critical path**. If a desktop-only
"see it on the surface" view is ever wanted it can come back as an enhancement,
but nothing in the exchange should depend on it.

## The three screens

Prototyped against the real catalog 2026-07-19. **The unit of the exchange is
the plug-in; a plug-in carries N user maps.** That relationship is the product,
and it is what the first draft of this plan missed entirely by designing a
detail-page rendering before designing the exchange around it.

### 1. `/mappings` — browse. **One row per PLUG-IN, not per map.**

The index lists plug-ins. Listing maps means "FabFilter Pro-Q 3" appearing
fourteen times down the page; at a few hundred maps that is not a list, it is
noise. One row per plug-in, the map count as a column, the maps themselves one
click away.

This supersedes "the index row" above, which specified a row per map — it read
fine against a 29-map corpus and falls apart at the volume the exchange is meant
to reach.

| Column | Notes |
| --- | --- |
| Plug-in | the link; left-aligned and dominant |
| Vendor | server field, captured at upload |
| Maps | count |
| Surfaces | union of UC1 / UF8 / UC1+UF8 across its maps |
| Best coverage | the best of its maps, with a bar |
| Works | best "works for me" count |

Search box over plug-in name; facet rail (surface · vendor · uses); sort (A–Z ·
most mappings · newest); paginated, sticky header.

**No coverage strip on this screen either.** The strip is per-map and this row is
a plug-in, so it has nothing to show. Verified at 180 rows: ~35 px per row, one
line each, no thumbnails, no cards — scannable.

### 2. `/mappings/plugin/<slug>` — several maps for one plug-in

**The screen that decides whether this is a service or a file dump**, and the one
that has to be got right.

**It must show a per-control DIFF, not N per-map summaries.** Evidence, from the
only genuine same-plug-in pair in the 29-map corpus — `FabFilter Pro-C 2` and
`Pro-C 2`:

- Under a per-map summary both read **"7 of 8, Bus Comp"**. Identical. A visitor
  would conclude they are interchangeable.
- They are not. Six controls carry the same parameter; **MIX carries `Wet` in one
  and `Dry Gain` in the other**, and the second binds an extra off-face slot
  (`linkIdx 46`, no UC1 control) also to `Dry Gain`.

A summary that hides the only difference is worse than no comparison — it
actively misinforms. So: rows are the union of controls bound by any map in the
group, columns are the maps, cells are the bound parameter name, and differing
rows are highlighted, with a headline count ("8 bound controls between them — 6
identical, 2 different"). Off-face bindings get their own rows rather than being
dropped.

**Comparison is a selection, not the default view.** A diff table stops being
readable past about four columns, so a popular plug-in carrying fifteen maps
cannot diff them all. The screen is a compact list — one row per map, carrying
the coverage strip, the coverage number, the derived capability tags and the
author — with checkboxes; tick two or three and diff those. The per-map coverage
strip belongs here, where the rows genuinely are maps.

### Coverage is domain-scoped, and UF8 has a different denominator entirely

- **UC1 Channel Strip:** out of 33. **Bus Comp:** out of 8.
- **UF8-only:** out of **128 V-Pot slots** (2 fader banks × 8 V-Pot banks × 8
  strips) plus 16 strip bindings — `UserPluginCatalog.h:402/:470`.

Two traps, both hit while prototyping:

1. **The on-disk keys are `banksByFaderBank` / `stripsByFaderBank`**, not the
   `banks` / `strips` of the C++ struct. Reading the struct names yields zero and
   every UF8 row renders as `0/0` — which looks like "nobody mapped anything"
   rather than like a bug.
2. **Do not count by recursing for any `vst3Param` you can find.** Some slots
   carry nested structures with their own `vst3Param`, so a recursive count
   returned **130 bound slots out of a possible 128**. A slot is a slot: walk the
   fixed three-level shape. Assert `n <= d` — the impossible number is what
   exposed the bug, and it would have shipped as a plausible-looking one on any
   map that happened to stay under the cap.

### 3. `/mappings/<id>/<slug>` — one map

Metadata and CTA left; the faceplate — **the only place it appears in the whole
site** — right; the control → parameter table beneath it.

**The one primitive that is not a mechanical move:** `drawTextCentered_`
centres by calling `ImGui_CalcTextSize`. An SVG sink has no font metrics — but
it does not need them, because `text-anchor="middle"` and
`dominant-baseline="central"` make the renderer do the centring. The SVG sink
ignores the measurement entirely. Every other primitive is a straight
translation.

### What the preview shows

- The full face, controls **dimmed** by default.
- Bound controls **lit**, labelled with the parameter name.
- Out-of-domain sections dimmed — the `domain` field on each control already
  drives exactly this in the app (`dimDomain` in `drawUc1Face_`), so the rule
  is inherited rather than reinvented.
- **Coverage, as a number**: "23 of 33 Channel Strip controls mapped".
  Derivable server-side and probably the single most useful thing on the page
  for deciding whether to download.

  **The denominator is domain-scoped, not the table total.** `kUc1Controls[]`
  holds 41 entries — 33 `ChannelStrip`, 8 `BusComp` (counted 2026-07-19). A
  fully-mapped Bus Comp map covers 8 controls; scoring it out of 41 would show
  a complete map as 20 % and make good work look lazy. Take the denominator
  from the map's own domain.
- **Modifier layers.** Slots carry `modLayers[]` (Option / Control /
  Control+Option). Default the view to Normal and offer a layer switch. A
  control mapped *only* on a modifier layer must look different from an
  unmapped one, or the Normal view understates the map.

### UF8 maps need a different shape — do not force the face metaphor

A UF8 map is `banks[2][8][8]` V-Pot slots (128) plus `strips[2][8]`
fader/solo/cut/sel bindings (16) — verified at `UserPluginCatalog.h:402` and
`:470`. A hardware face renders the 16 strip bindings well and the 128 V-Pot
cells badly.

So: face render for the strip bindings, and a compact **8 × 8 grid per V-Pot
bank** with a bank selector for the rest. Two views, one page.

### The failure mode to design against

`kUc1Controls[]`'s own comment lists slots that have **no UC1 control** — Fader,
Pan, Width, Output Trim, Bypass, QA1–6, SAT, SAT.I, GRP for CS; Bypass and GRP
for BC. A map binding those is perfectly valid.

**A linkIdx with no entry in the table must never silently vanish.** It renders
in an "also mapped" list beneath the face. Dropping it quietly is what would
make a thorough map look sparse — the exact misjudgement the preview exists to
prevent.

Guard it in CI: assert every linkIdx used by the built-in `PluginMap` resolves
either to a control or to the extras list. Otherwise a future control rename
blanks part of the preview with a green build — the same class of silent
failure as `astro build` exiting 0 on a render error.

### Indexing and accessibility

`/mappings` is server-rendered so that search engines see it. An inline SVG
keeps that property as long as the labels are real `<text>` nodes — which they
are, since the exporter emits text rather than paths.

Emit the **control → parameter table as real HTML** beneath the preview
regardless. The picture is the hook; the table is what a screen reader reads,
what a text search matches, and what survives if the SVG fails to load.

### Where it sits in the phasing

The exporter is **Phase 2a** — it must land before `/mappings` ships, because
the detail page is not worth publishing without it.

## Phases

### Phase 1 — `.rea60map`, one file for one map — **DONE** (`2befc2f`)

Extension only, no networking. Mappings are shareable by file today, over
forums or Discord, before any server exists.

The envelope as actually shipped (`MapShare`, and `serialize_` in the same
commit). The server indexes on the top level and never has to unescape `map`:

```json
{
  "format": "rea-sixty-map",
  "version": 1,
  "plugin":      "…",   // mirrors the map's own `match`
  "vendor":      "…",   // user-entered; not derivable from `match`
  "surfaces":    "…",   // "uc1" | "uf8" | "uc1+uf8", via surfaceScope()
  "author":      "…",
  "description": "…",
  "licence":     "…",   // SPDX id; "CC0-1.0" for exchange uploads
  "created_at":  0,     // unix seconds
  "map":         "…"    // the UserPluginMap, escaped
}
```

Note what is **not** in the envelope but appears in the older sketch above:
there is no `id` and no `revision`. The server has to mint the id at upload;
do not expect the file to carry one. Re-uploads therefore need an explicit
"this replaces my earlier map for this plug-in" step rather than an automatic
revision bump.

Also shipped: per-row **Export…**/**Import…**, `paramSnapshot` pruned to the
params any slot / cache / macro step / UF8 bank / strip binding actually
references, and `isDefault` forced off in both directions so a shared map can
never claim the recipient's Default slot.

### Phase 2a — face exporter (blocks the detail page)

Standalone CLI in `extension/tools/`, no REAPER and no libusb needed. Gives
`VCanvas` a virtual sink, keeps the current bodies as the ImGui sink, adds an
SVG sink, and writes `uc1-face.svg` / `uf8-face.svg` plus the two control JSON
files. Artefacts are committed so the website build stays toolchain-free.
Full reasoning under **The visual preview** above.

### Phase 2 — reasixty.com + api.reasixty.com

Server. Runs on the existing Hostinger VPS; Docker and Caddy are already there.

- SQLite plus files on disk.
- Auth per the **Auth** section: passkey primary, magic-link equal fallback,
  optional recovery email. Privacy policy live before the first sign-up.
- Detail page carries the visual preview plus the HTML control→parameter table.
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
