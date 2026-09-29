# Plan: licence notices for bundled third-party code

Status 2026-09-29: plan only, nothing built.

## Why

Every release ships libusb and hidapi as separate libraries, and the Stream Deck
plug-in ships `ws`. No release carries a single licence notice, licence text or
source offer. That is independent of Rea-Sixty's own licence.

- **libusb, LGPL-2.1-or-later.** §4: object code must be accompanied by the source,
  or access to copy it "from the same place". §6: prominent notice that the library
  is used, a copy of the licence, and, if the work displays copyright notices while
  running, libusb's among them. (Read from gnu.org/licenses/old-licenses/lgpl-2.1.txt.)
- **hidapi, BSD-3-Clause option.** Binary redistribution must reproduce the copyright
  notice in the documentation or other materials shipped with it.
- **ws, MIT** (Stream Deck plug-in, `node_modules/ws`). Notice must go with copies.
- Compiled in, zlib-style, no obligation for binaries, acknowledgement appreciated:
  REAPER SDK (`vendor/reaper-sdk/sdk/LICENSE`), WDL (per-file headers).

## What is bundled, per platform

| Package | libusb | hidapi | Where the version comes from |
|---|---|---|---|
| macOS arm64 zip + ReaPack | Homebrew, 1.0.29 on the Mac Studio today | Homebrew 0.15.0 | built locally, `brew list --versions` |
| macOS x86_64 (ReaPack) | Homebrew on `macos-15-intel` | Homebrew | CI, version at build time |
| Windows zip + ReaPack | 1.0.30 prebuilt (`build.yml:102`) | 0.15.0 prebuilt (`build.yml:104`) | CI download URLs |
| Linux tar + ReaPack | Ubuntu 22.04 package | Ubuntu 22.04 package | CI container, `dpkg-query` |
| `com.reasixty.companion.streamDeckPlugin` | | | `ws` from `package.json` (^8.18.0) |
| ORC.app (DMG) | 1.0.30, built universal from the official tarball (`orc_libusb` in `extension/CMakeLists.txt`) | not linked | the pinned tarball is the source to offer |

The Linux libraries are Ubuntu's builds, so their corresponding source is Ubuntu's
source package (`apt-get source`), including Ubuntu's patches.

## Steps

1. **Notices and texts in the repo.** `dist/THIRD-PARTY-NOTICES.txt` (component,
   licence, copyright holder, upstream URL, where to get the source) and
   `dist/licenses/` with the full texts, each fetched from the project's own
   repository, not written from memory.
2. **Versions recorded where the build happens.** CI writes a small
   `third-party-versions.txt` per job (brew, the URL versions on Windows,
   `dpkg-query` in the Linux container). The Mac arm64 release build writes the
   same locally. The notices get the real version per platform.
3. **Into every archive.** `dist/release-mac.sh`, `dist/release-linux.sh`, the
   hand-assembled Windows zip in the runbook, and the Stream Deck package (make
   sure `node_modules/ws/LICENSE` is inside).
4. **Into ReaPack.** `reaper-scripts/Rea-Sixty/Rea-Sixty.ext` gets `@provides`
   lines for the notices file and the licence texts. reapack-index has quirks
   (see memory `intel-mac-reapack-pipeline`), so the syntax is checked against a
   dry `reapack-index --check` before release.
5. **libusb source with every release.** The upstream tarball for the Mac and
   Windows versions, the Ubuntu source package for Linux (pulled in the CI
   container), uploaded as release assets.
6. **About tab.** A short "Third-party" block in `SettingsScreen::drawAbout`:
   libusb (LGPL-2.1), hidapi (BSD-3-Clause), REAPER SDK and WDL (Cockos, zlib),
   and where the notices file sits. Covers the §6 copyright display.
7. **A check, not a habit.** `dist/check-notices.py`, called by every packer:
   fails if a bundled library in the archive has no notice entry, if a licence
   text is missing, or if the LGPL source tarball for that version is not staged.
8. **Website.** `web/src/pages/legal.astro` says the REAPER SDK licence "could NOT
   be confirmed". `vendor/reaper-sdk/sdk/LICENSE` exists and is zlib-style.
   Correct it and link the notices file.
9. **Runbook.** `.local-docs/release-process.md` gets the source-tarball step and
   the check.
10. **Manual.** The About section of `docs/user-manual.md` names the new block.
11. **Changelog line** for the next release.

## Open

- Old releases (61): add the notices and the matching libusb source to each
  existing release after the fact? The URLs in `index.xml` do not change. The
  libusb version per old release can be read from the local `dist/stage-*`
  folders.
- ReaImGui: only the generated API header `vendor/reaimgui/reaper_imgui_functions.h`
  is compiled in. Its licence terms are not checked yet.
- Linux links libstdc++ and libgcc statically (`extension/CMakeLists.txt:574`).
  Whether the GCC Runtime Library Exception leaves any obligation is not checked yet.
