# Rea-Sixty Release Runbook

End-to-end runbook for shipping a new tagged version to ReaPack across macOS / Windows / Linux. **Optimised so Frank only needs to say "deploy auf reapack als neuer tag"** — every other detail (IPs, paths, scripts, gotchas) lives here.

## What to ask Frank up front

Default: bump the patch version (last tag + 0.0.1). Ask Frank ONLY if:
- The diff since last tag looks like substantive new functionality (then offer minor bump).
- He's already told you the version to use.

Don't ask about IPs, paths, or platform availability — they're below.

## Repos

| Repo | Path | Purpose |
| --- | --- | --- |
| `acklin83/Rea-Sixty` | `~/Documents/dev/reaper-uf8` | Source + GitHub Release artefacts |
| `acklin83/reaper-scripts` | `~/Documents/dev/reaper-scripts` | ReaPack metapackage `Rea-Sixty/Rea-Sixty.ext`; GH-Action regens `index.xml` |

User-facing ReaPack URL: `https://github.com/acklin83/reaper-scripts/raw/main/index.xml`

## Hosts

| Host | IP | User | Auth | Repo path | Notes |
| --- | --- | --- | --- | --- | --- |
| Mac (local) | — | stoersender | — | `~/Documents/dev/reaper-uf8` | dev box |
| Windows | `192.168.177.197` | `claude` | password `claudepass` | `C:\Users\claude\reaper-uf8` | dual-boot box |
| Linux | `192.168.177.197` | `frank` | ssh key | `~/reaper-uf8` | same physical box, separate OS |

**Dual-boot gotcha:** Win and Linux share the IP. Booting between them flips the SSH host key → next connection bombs with `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED`. Standard recovery before every SSH (Frank already authorized this pattern):

```bash
ssh-keygen -R 192.168.177.197
ssh -o StrictHostKeyChecking=accept-new <user>@192.168.177.197 ...
```

Before SSHing to Linux, confirm the box has been booted into Linux. If `ssh frank@...` errors with `Connection refused` or lands on cmd.exe, Frank still needs to reboot. Ask him then.

---

## Step-by-step

Pick a version and put it in a shell variable up front:

```bash
NEW_TAG=v0.1.5          # bump from last `git describe --tags --abbrev=0`
VERSION="${NEW_TAG#v}"  # 0.1.5
```

### 1. Pre-flight

```bash
cd ~/Documents/dev/reaper-uf8
git status                                           # working tree clean? (uncommitted change → commit first)
git log $(git describe --tags --abbrev=0)..HEAD --oneline   # what's new since last tag
```

If there are uncommitted changes, commit them with a normal `feat:` / `fix:` / `refactor:` message and the Co-Authored-By trailer (CLAUDE.md convention). Push to main.

### 2. Write release notes

`dist/RELEASE-NOTES-${NEW_TAG}.md`. Follow the v0.1.4 / v0.1.5 template:

- One-paragraph release theme summary at top
- `## Install via ReaPack` block (copy verbatim from prior release)
- `## What's new` grouped by area (Device pane, SEL-Mode, Bug fixes, etc.) — each bullet calls out user-visible change + reason
- `## Known issues`
- `## Manual install` block (copy from prior release with version updated)

Commit + push:

```bash
git add dist/RELEASE-NOTES-${NEW_TAG}.md
git commit -m "release: ${NEW_TAG} notes"
git push origin main
```

### 3. Tag

**Convention:** tag points at the release-notes commit (i.e., HEAD after step 2). v0.1.4 followed this; v0.1.5 deviated — go back to this convention.

```bash
git tag -a "$NEW_TAG" -m "Rea-Sixty $NEW_TAG"
git push origin "$NEW_TAG"
```

### 4. Build all three platforms

These three can run in parallel from the Mac.

#### Mac (local)

```bash
cmake --build extension/build
```

#### Windows

```bash
ssh-keygen -R 192.168.177.197
ssh -o StrictHostKeyChecking=accept-new claude@192.168.177.197 \
  "cd C:\Users\claude\reaper-uf8 && git fetch --tags && git checkout $NEW_TAG && git pull --ff-only 2>&1 | tail -3"
ssh claude@192.168.177.197 "cd C:\Users\claude && build_rea.bat" 2>&1 | tail -10
```

Always rebuild even if Frank says "Windows läuft schon" — his pre-release build may be stale (predates fixes Claude pushed during the session).

#### Linux

```bash
ssh-keygen -R 192.168.177.197       # only if not already done in this session
ssh -o StrictHostKeyChecking=accept-new frank@192.168.177.197 \
  "cd ~/reaper-uf8 && git fetch --tags && git checkout $NEW_TAG && cmake --build extension/build 2>&1 | tail -5"
```

**Repo lives at `~/reaper-uf8` on Linux, NOT `~/Rea-Sixty`.** Past doc lied; the dir name on Linux matches Mac's working dir (memory-anchor convention).

### 5. Package Mac (sign + notarize)

```bash
./dist/release-mac.sh
```

Reads `$NEW_TAG` from `git describe --tags --abbrev=0`, signs the three dylibs with Developer ID, submits to Apple notary cloud, verifies. Output: `dist/rea-sixty-mac-${NEW_TAG}.zip` + `dist/stage-${NEW_TAG}/` with three notarised dylibs. 1–10 minutes.

### 6. Package Windows

```bash
ssh claude@192.168.177.197 'cd C:\Users\claude\reaper-uf8 && powershell -ExecutionPolicy Bypass -File dist\release-win.ps1 -LibusbDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\libusb-1.0.dll" -HidapiDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\hidapi.dll"' 2>&1 | tail -8

mkdir -p dist/stage-win-${NEW_TAG}
scp claude@192.168.177.197:"C:/Users/claude/reaper-uf8/dist/rea-sixty-win-${NEW_TAG}.zip" dist/
scp claude@192.168.177.197:"C:/Users/claude/reaper-uf8/dist/stage-win-${NEW_TAG}/*.dll" dist/stage-win-${NEW_TAG}/
```

The `stage-win-v0.1.1` paths are fallbacks — Frank's Win box has no `LIBUSB_ROOT` / `HIDAPI_ROOT` env vars set. Reuse the v0.1.1 DLLs forever (libusb / hidapi don't churn release-to-release).

### 7. Package Linux

```bash
ssh frank@192.168.177.197 "cd ~/reaper-uf8 && ./dist/release-linux.sh 2>&1 | tail -5"
scp frank@192.168.177.197:"~/reaper-uf8/dist/rea-sixty-linux-${NEW_TAG}.tar.gz" dist/
scp frank@192.168.177.197:"~/reaper-uf8/extension/build/reaper_rea-sixty.so" "dist/stage-linux-${NEW_TAG}.so"
```

### 8. Create GitHub Release

```bash
# CRITICAL: rename the .so locally before upload (see "Pitfall: gh #rename
# does NOT rename" below). Use /tmp so it doesn't pollute dist/.
cp "dist/stage-linux-${NEW_TAG}.so" /tmp/reaper_rea-sixty.so

gh release create "$NEW_TAG" \
    --title "Rea-Sixty $NEW_TAG" \
    --notes-file "dist/RELEASE-NOTES-${NEW_TAG}.md" \
    "dist/rea-sixty-mac-${NEW_TAG}.zip" \
    "dist/rea-sixty-win-${NEW_TAG}.zip" \
    "dist/rea-sixty-linux-${NEW_TAG}.tar.gz" \
    "dist/stage-${NEW_TAG}/reaper_rea-sixty.dylib" \
    "dist/stage-${NEW_TAG}/libusb-1.0.0.dylib" \
    "dist/stage-${NEW_TAG}/libhidapi.0.dylib" \
    "dist/stage-win-${NEW_TAG}/reaper_rea-sixty.dll" \
    "dist/stage-win-${NEW_TAG}/libusb-1.0.dll" \
    "dist/stage-win-${NEW_TAG}/hidapi.dll" \
    /tmp/reaper_rea-sixty.so

rm /tmp/reaper_rea-sixty.so

# Verify all 10 ReaPack assets present with exact expected names
gh release view "$NEW_TAG" --json assets --jq '.assets[].name' | sort
```

Expected output (the three bundle zips/tarballs + the 7 individual binaries ReaPack downloads):

```
hidapi.dll
libhidapi.0.dylib
libusb-1.0.0.dylib
libusb-1.0.dll
rea-sixty-linux-v0.1.5.tar.gz
rea-sixty-mac-v0.1.5.zip
rea-sixty-win-v0.1.5.zip
reaper_rea-sixty.dll
reaper_rea-sixty.dylib
reaper_rea-sixty.so
```

#### Pitfall: `gh` `#rename` does NOT rename

`gh release create FILE#DISPLAYLABEL` sets the *display label* in the UI, not the uploaded filename. The basename of the local path is the server filename. ReaPack downloads by filename, so a wrong basename = 404 at install. This burnt v0.1.5's first upload pass.

**Always rename the file on disk before passing it to gh.** Don't trust `#`.

### 9. Update ReaPack metapackage

```bash
cd ~/Documents/dev/reaper-scripts
```

Edit `Rea-Sixty/Rea-Sixty.ext`:

- Update `@version` to the new version (without the `v` prefix — ReaPack adds the `v` itself via `v$version` in the URLs).
- Replace the `@changelog` block with bullets matching the release-notes "What's new" content. Keep two-space indent, blank line between bullets. Don't include the install/manual-install boilerplate — just the changes.

The `@provides` block doesn't change between releases (URLs use `v$version` and `$path` substitutions). Leave it alone.

Commit + push:

```bash
git pull --rebase             # someone else may have pushed (rare but cheap)
git add Rea-Sixty/Rea-Sixty.ext
git commit -m "Rea-Sixty $NEW_TAG"
git push
```

### 10. Wait for the ReaPack GH-Action + verify

The push triggers `.github/workflows/reapack.yml` which regenerates `index.xml`. Poll until it finishes:

```bash
until [ "$(gh -R acklin83/reaper-scripts run list --limit 1 --json status --jq '.[0].status')" = "completed" ]; do sleep 5; done
gh -R acklin83/reaper-scripts run list --limit 1 --json conclusion --jq '.[0].conclusion'   # → "success"

# Verify the new version landed
curl -sL https://github.com/acklin83/reaper-scripts/raw/main/index.xml | grep -B1 -A2 "name=\"${VERSION}\"" | head -10
```

`gh run list` is faster than `curl`-polling `index.xml` because the action takes ~80–90 seconds and the index doesn't update until the post-action commit hits main.

### 11. Final verify

- `gh release view "$NEW_TAG" --web` — visually confirm all 10 assets attached + release notes rendered.
- (Optional) In REAPER on Mac: Extensions → ReaPack → Refresh repositories → Browse packages → look for the new version.

---

## Cheat sheet — single block

When Frank says "deploy auf reapack als neuer tag" and the working tree is already committed + pushed, the entire flow is:

```bash
# Set once
NEW_TAG=v0.1.6
VERSION="${NEW_TAG#v}"

# 1. Notes
$EDITOR "dist/RELEASE-NOTES-${NEW_TAG}.md"
git add "dist/RELEASE-NOTES-${NEW_TAG}.md"
git commit -m "release: ${NEW_TAG} notes"
git push origin main

# 2. Tag
git tag -a "$NEW_TAG" -m "Rea-Sixty $NEW_TAG"
git push origin "$NEW_TAG"

# 3. Build all three (parallel-safe; runs sequentially below for clarity)
cmake --build extension/build
ssh-keygen -R 192.168.177.197
ssh -o StrictHostKeyChecking=accept-new claude@192.168.177.197 \
  "cd C:\Users\claude\reaper-uf8 && git fetch --tags && git checkout $NEW_TAG"
ssh claude@192.168.177.197 "cd C:\Users\claude && build_rea.bat"
ssh-keygen -R 192.168.177.197
ssh -o StrictHostKeyChecking=accept-new frank@192.168.177.197 \
  "cd ~/reaper-uf8 && git fetch --tags && git checkout $NEW_TAG && cmake --build extension/build"

# 4. Package
./dist/release-mac.sh
ssh claude@192.168.177.197 'cd C:\Users\claude\reaper-uf8 && powershell -ExecutionPolicy Bypass -File dist\release-win.ps1 -LibusbDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\libusb-1.0.dll" -HidapiDll "C:\Users\claude\reaper-uf8\dist\stage-win-v0.1.1\hidapi.dll"'
mkdir -p "dist/stage-win-${NEW_TAG}"
scp claude@192.168.177.197:"C:/Users/claude/reaper-uf8/dist/rea-sixty-win-${NEW_TAG}.zip" dist/
scp claude@192.168.177.197:"C:/Users/claude/reaper-uf8/dist/stage-win-${NEW_TAG}/*.dll" "dist/stage-win-${NEW_TAG}/"
ssh frank@192.168.177.197 "cd ~/reaper-uf8 && ./dist/release-linux.sh"
scp frank@192.168.177.197:"~/reaper-uf8/dist/rea-sixty-linux-${NEW_TAG}.tar.gz" dist/
scp frank@192.168.177.197:"~/reaper-uf8/extension/build/reaper_rea-sixty.so" "dist/stage-linux-${NEW_TAG}.so"

# 5. GH Release
cp "dist/stage-linux-${NEW_TAG}.so" /tmp/reaper_rea-sixty.so
gh release create "$NEW_TAG" \
    --title "Rea-Sixty $NEW_TAG" \
    --notes-file "dist/RELEASE-NOTES-${NEW_TAG}.md" \
    "dist/rea-sixty-mac-${NEW_TAG}.zip" \
    "dist/rea-sixty-win-${NEW_TAG}.zip" \
    "dist/rea-sixty-linux-${NEW_TAG}.tar.gz" \
    "dist/stage-${NEW_TAG}/reaper_rea-sixty.dylib" \
    "dist/stage-${NEW_TAG}/libusb-1.0.0.dylib" \
    "dist/stage-${NEW_TAG}/libhidapi.0.dylib" \
    "dist/stage-win-${NEW_TAG}/reaper_rea-sixty.dll" \
    "dist/stage-win-${NEW_TAG}/libusb-1.0.dll" \
    "dist/stage-win-${NEW_TAG}/hidapi.dll" \
    /tmp/reaper_rea-sixty.so
rm /tmp/reaper_rea-sixty.so

# 6. Metapackage
cd ~/Documents/dev/reaper-scripts
$EDITOR Rea-Sixty/Rea-Sixty.ext     # bump @version + replace @changelog
git pull --rebase
git add Rea-Sixty/Rea-Sixty.ext
git commit -m "Rea-Sixty ${NEW_TAG}"
git push

# 7. Wait + verify
until [ "$(gh -R acklin83/reaper-scripts run list --limit 1 --json status --jq '.[0].status')" = "completed" ]; do sleep 5; done
curl -sL https://github.com/acklin83/reaper-scripts/raw/main/index.xml | grep "name=\"${VERSION}\"" | head -2
```

---

## Pitfalls captured from prior releases

- **`gh release create FILE#RENAME` does NOT rename the uploaded file.** Sets display label only. Rename on disk first. (Burnt v0.1.5 first pass — Linux `.so` uploaded as `stage-linux-v0.1.5.so`; ReaPack 404s on every Linux user's install.) The original release-process.md said `<local>#<server>` works — it doesn't. Trust the new doc.
- **Dual-boot host-key flip.** Win and Linux share the IP, different SSH host keys. Every OS switch needs `ssh-keygen -R 192.168.177.197` first. Frank pre-authorized this — don't ask again.
- **Linux repo path is `~/reaper-uf8`, not `~/Rea-Sixty`.** Past doc was wrong.
- **Always rebuild Windows even if Frank says "läuft schon".** Frank's pre-release builds usually predate Claude's session-end fixes by 30–60 minutes.
- **The Windows release script needs explicit DLL paths.** Frank's Win box has no `LIBUSB_ROOT` / `HIDAPI_ROOT` set. Use the v0.1.1 stage paths as a permanent fallback.
- **`pwsh` vs `powershell` on Win.** Frank's box only ships `powershell.exe`.
- **Notarisation keychain profile.** `xcrun notarytool store-credentials rea-sixty-notarize` was set up once; the secret lives in macOS Keychain. If the cert rolls over or Frank moves machines, re-run that command and update `SIGN_IDENTITY` in `release-mac.sh`.
- **`dist/` artefacts.** The `dist/` directory accumulates per-release tarballs / zips / stage dirs. `.gitignore` covers `*.zip` only; `.tar.gz` / `.rules` / `.so` / `.md` files have been untracked for several releases. **Only commit `dist/RELEASE-NOTES-*.md`** — everything else is a release artefact hosted on GitHub, not source.
- **No GH-Actions for builds-on-tag.** The matrix-build write-up sits in `.claude/memory/handoff-2026-05-19-reapack-prep.md` item 4 and stays queued. Until then it's the manual three-box dance documented here.
