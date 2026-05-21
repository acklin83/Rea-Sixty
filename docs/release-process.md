# Rea-Sixty Release Process

Manual runbook for shipping a tagged version with notarised macOS, Windows, and Linux binaries plus ReaPack metadata. No GitHub Actions; everything runs from the Mac dev box, with the Windows DLL + Linux .so built remotely via SSH.

Three repos involved:
- **`acklin83/Rea-Sixty`** — source + GitHub Release artefacts (this repo)
- **`acklin83/reaper-scripts`** — holds the metapackage `Rea-Sixty/Rea-Sixty.ext` that ReaPack scans. Its `.github/workflows/reapack.yml` regenerates `index.xml` on every push.
- (No third repo. `reapack-index` runs in the scripts repo's GH-Action.)

ReaPack URL users paste: `https://github.com/acklin83/reaper-scripts/raw/main/index.xml`

---

## Pre-flight

```bash
# Confirm working tree clean + on main
git status
git log --oneline origin/main..HEAD   # empty = pushed
```

Decide version (semver patch by default — minor only for substantive new modes). Inspect commits since last tag to draft the release notes:

```bash
git log $(git describe --tags --abbrev=0)..HEAD --oneline
```

---

## Step 1 — Tag

```bash
NEW_TAG=v0.1.2
git tag -a "$NEW_TAG" -m "Rea-Sixty $NEW_TAG"
git push origin "$NEW_TAG"
```

Tag pushed first so the remote build boxes can `git fetch --tags && git checkout <tag>` (or just pull main if the tag points at HEAD). Both `release-mac.sh` and `release-win.ps1` read `git describe --tags --abbrev=0` for the version that ends up in artefact filenames.

---

## Step 2 — Build all three platforms

These three can run in parallel. The Mac build is local; Windows + Linux go through SSH.

### Mac (local)

```bash
cmake --build extension/build
ls -la extension/build/*.dylib   # reaper_rea-sixty / libusb-1.0.0 / libhidapi.0
```

### Windows (SSH `claude@192.168.177.197`, password `claudepass`)

The repo lives at `C:\Users\claude\reaper-uf8`. `build_rea.bat` in the user's home calls `vcvars64.bat` + `cmake --build ... --config Release`. The DLL dependencies were copied into `dist\stage-win-v0.1.1\` during the v0.1.1 release; reuse those paths or override on the command line.

```bash
ssh claude@192.168.177.197 "cd C:\Users\claude\reaper-uf8 && git fetch --tags && git pull --ff-only"
ssh claude@192.168.177.197 "cd C:\Users\claude && build_rea.bat"
```

### Linux (SSH `frank@<linux-box>` — dual-boot on Frank's Win box, ask before assuming online)

```bash
ssh frank@<linux-ip> "cd ~/Rea-Sixty && git fetch --tags && git pull --ff-only && cmake --build extension/build"
```

`reaper_rea-sixty.so` lands in `extension/build/`.

---

## Step 3 — Package + sign + notarize Mac

`dist/release-mac.sh` reads the tag, copies the three dylibs into `dist/stage-<tag>/`, re-signs them with `Developer ID Application: Frank Acklin (234VF7874N)` + hardened runtime + timestamp, zips them, submits to Apple's notary cloud via the `rea-sixty-notarize` keychain profile, and verifies acceptance via `spctl`.

```bash
./dist/release-mac.sh
```

Takes 1–10 min. Output: `dist/rea-sixty-mac-<tag>.zip`. The same `dist/stage-<tag>/` directory holds the three notarised dylibs as individual files for the per-binary ReaPack upload.

Notarisation tickets can't be stapled to loose dylibs (Apple only supports stapling on `.app/.dmg/.pkg`). The notarisation registers each dylib hash with Apple's cloud; Gatekeeper online-checks first-load. No action needed beyond running the script.

---

## Step 4 — Package Windows

On the Win box, `release-win.ps1` packs the three DLLs into `dist\rea-sixty-win-<tag>.zip`. Default paths look for `libusb-1.0.dll` + `hidapi.dll` under `LIBUSB_ROOT` / `HIDAPI_ROOT` env vars — those aren't set on Frank's box, so pass overrides pointing at a previous release's stage dir:

```bash
ssh claude@192.168.177.197 'cd C:\Users\claude\reaper-uf8 && powershell -ExecutionPolicy Bypass -File dist\release-win.ps1 -LibusbDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\libusb-1.0.dll" -HidapiDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\hidapi.dll"'
```

Then pull the zip + the staged DLLs back:

```bash
scp claude@192.168.177.197:'C:/Users/claude/reaper-uf8/dist/rea-sixty-win-v0.1.2.zip' dist/
mkdir -p dist/stage-win-v0.1.2
scp claude@192.168.177.197:'C:/Users/claude/reaper-uf8/dist/stage-win-v0.1.2/*.dll' dist/stage-win-v0.1.2/
```

Use `pwsh` only if installed — Frank's box ships stock `powershell.exe`. Forward slashes in the scp source path are required (bash glob expansion happens locally).

No code signing — Windows users get a "Publisher unknown" SmartScreen warning on the WinUSB installer regardless of how we sign the DLL itself (the warning is about the .CAT minted by the installer, not our DLL). A code-signing cert wouldn't suppress it. Signing the DLL would only suppress the irrelevant Mark-of-the-Web warning; not worth the cert cost.

---

## Step 5 — Package Linux

`release-linux.sh` runs on the Linux box. It copies `reaper_rea-sixty.so`, writes a `99-rea-sixty.rules` udev rule + `INSTALL.txt`, and tars them.

```bash
ssh frank@<linux-ip> "cd ~/Rea-Sixty && ./dist/release-linux.sh"
scp frank@<linux-ip>:~/Rea-Sixty/dist/rea-sixty-linux-v0.1.2.tar.gz dist/
scp frank@<linux-ip>:~/Rea-Sixty/extension/build/reaper_rea-sixty.so dist/stage-linux-v0.1.2.so
```

The `.so` is also uploaded individually for ReaPack — see Step 7.

No libusb/hidapi bundling. Linux users install those from their distro's package manager (`sudo apt install libusb-1.0-0 libhidapi-hidraw0`).

---

## Step 6 — Write release notes

`dist/RELEASE-NOTES-v<tag>.md`. Follow the v0.1.1 / v0.1.2 template:
- One-line summary of the release theme
- Install via ReaPack instructions
- Per-section "What's new" grouped by area (Device pane, SEL-Mode, Bug fixes, etc.) — each bullet calls out user-visible change and reason
- Known issues block
- Manual install fallback links

Commit the file to the repo so it ships alongside the tag.

```bash
git add dist/RELEASE-NOTES-v0.1.2.md
git commit -m "release: v0.1.2 notes"
git push origin main
```

---

## Step 7 — Create GitHub Release

```bash
gh release create v0.1.2 \
    --title "Rea-Sixty v0.1.2" \
    --notes-file dist/RELEASE-NOTES-v0.1.2.md \
    dist/rea-sixty-mac-v0.1.2.zip \
    dist/rea-sixty-win-v0.1.2.zip \
    dist/rea-sixty-linux-v0.1.2.tar.gz \
    dist/stage-v0.1.2/reaper_rea-sixty.dylib \
    dist/stage-v0.1.2/libusb-1.0.0.dylib \
    dist/stage-v0.1.2/libhidapi.0.dylib \
    dist/stage-win-v0.1.2/reaper_rea-sixty.dll \
    dist/stage-win-v0.1.2/libusb-1.0.dll \
    dist/stage-win-v0.1.2/hidapi.dll \
    dist/stage-linux-v0.1.2.so#reaper_rea-sixty.so
```

The 3 bundles are for manual download. The 7 individual binaries (3 Mac dylibs + 3 Win DLLs + 1 Linux .so) are what ReaPack downloads per platform.

**Critical:** the Linux `.so` filename on the server MUST be exactly `reaper_rea-sixty.so` (not `stage-linux-v0.1.2.so`). Use `gh release upload`'s rename syntax `<local-path>#<server-name>` shown above. ReaPack derives the install filename from the URL.

---

## Step 8 — Update ReaPack metapackage

The metapackage lives at `acklin83/reaper-scripts/Rea-Sixty/Rea-Sixty.ext`. ReaPack scans this file; `reapack-index` regenerates `index.xml` from it on every push (GH-Action `.github/workflows/reapack.yml`).

```bash
cd ~/Documents/dev/reaper-scripts
mkdir -p Rea-Sixty   # only if first release
```

Hand-edit `Rea-Sixty/Rea-Sixty.ext`:

```
@description Rea-Sixty
@author Frank Acklin
@version 0.1.2
@changelog
  (multi-line bullet list — same content as the release notes' "What's
   new" section, indented two spaces per ReaPack convention)
@provides
  [darwin-arm64] reaper_rea-sixty.dylib https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [darwin-arm64] libusb-1.0.0.dylib     https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [darwin-arm64] libhidapi.0.dylib      https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [win64] reaper_rea-sixty.dll https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [win64] libusb-1.0.dll       https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [win64] hidapi.dll           https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
  [linux64] reaper_rea-sixty.so https://github.com/acklin83/Rea-Sixty/releases/download/v$version/$path
@link
  GitHub https://github.com/acklin83/Rea-Sixty
  Issue tracker https://github.com/acklin83/Rea-Sixty/issues
@about
  # Rea-Sixty
  REAPER extension that drives the SSL UF8 + UC1 surfaces natively...
  (full marketing-pitch markdown — keep the structure aligned with TotalReaper.ext)
```

Commit + push:

```bash
cd ~/Documents/dev/reaper-scripts
git add Rea-Sixty/Rea-Sixty.ext
git commit -m "Rea-Sixty v0.1.2"
git push
```

The push triggers `.github/workflows/reapack.yml`, which:
1. Installs `pandoc` + `reapack-index` gem
2. Runs `reapack-index --rebuild --commit` regenerating `index.xml`
3. Commits + pushes the index update from `GitHub Actions`

User REAPER clients pick up the new version on next ReaPack repository refresh.

---

## Step 9 — Verify

- `gh release view v0.1.2 --web` — confirm all 10 assets attached
- `curl -sL https://github.com/acklin83/reaper-scripts/raw/main/index.xml | grep -A1 "Rea-Sixty"` — wait 1–2 min after the metapackage push for the workflow to finish, then confirm the new version landed in `index.xml`
- (Optional) In REAPER: Extensions → ReaPack → Refresh repositories, then look for the new version in Browse packages

---

## Known sharp edges

- **No GH-Actions for builds-on-tag.** The matrix-build write-up sits in [handoff-2026-05-19-reapack-prep](.claude/memory/handoff-2026-05-19-reapack-prep.md) item 4 and stays queued. Until then it's the manual three-box dance documented here.
- **`pwsh` vs `powershell` on the Win box.** Frank's box only has stock `powershell.exe`; the runbook uses that. PowerShell 7 (`pwsh`) is not installed.
- **Linux IP varies.** Frank's Linux is dual-boot on the same physical box as Windows. Confirm Linux is the booted OS before SSHing; otherwise SSH lands on cmd.exe and the build silently fails.
- **dist/ artefacts.** The `dist/` directory holds release tarballs / zips / stage dirs from prior releases. `.gitignore` covers `*.zip` only; the `.tar.gz`, `.rules`, and `.md` files have been untracked for several releases. Don't commit them unless explicitly asked — they're release artefacts hosted on GitHub, not source.
- **Notarization keychain profile.** `xcrun notarytool store-credentials rea-sixty-notarize` was set up once; the secret lives in macOS Keychain. If the cert rolls over or Frank moves machines, re-run that command and update the SIGN_IDENTITY in `release-mac.sh`.
