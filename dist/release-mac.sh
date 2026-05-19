#!/usr/bin/env bash
# Rea-Sixty macOS release: sign + notarize + verify the three bundled
# dylibs so ReaPack-installed copies clear Gatekeeper without quarantine
# prompts. Ad-hoc signing (the dev default) is enough to satisfy macOS
# 15's strict in-process signature check, but ReaPack downloads carry
# com.apple.quarantine — Gatekeeper online-checks the hash against
# Apple's notary cloud on first load.
#
# Pre-reqs:
#   - "Developer ID Application: Frank Acklin (234VF7874N)" in Keychain
#   - notarytool keychain profile "rea-sixty-notarize" (one-time setup
#     via `xcrun notarytool store-credentials rea-sixty-notarize ...`)
#   - Project built with `cmake --build extension/build` first
#
# Run:
#   ./dist/release-mac.sh
#
# Output: dist/rea-sixty-mac-v<VERSION>.zip containing all three
# notarized dylibs, ready for ReaPack-hosted distribution.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/extension/build"
DIST_DIR="$REPO_ROOT/dist"
SIGN_IDENTITY="Developer ID Application: Frank Acklin (234VF7874N)"
NOTARY_PROFILE="rea-sixty-notarize"

DYLIBS=(
    "reaper_rea-sixty.dylib"
    "libusb-1.0.0.dylib"
    "libhidapi.0.dylib"
)

# Version comes from git tag (`v0.1.0`) or HEAD short-sha as fallback.
VERSION="$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null \
            || git -C "$REPO_ROOT" rev-parse --short HEAD)"

STAGE="$DIST_DIR/stage-$VERSION"
ZIP_PATH="$DIST_DIR/rea-sixty-mac-$VERSION.zip"

echo "==> Release stage: $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"

for lib in "${DYLIBS[@]}"; do
    src="$BUILD_DIR/$lib"
    if [[ ! -f "$src" ]]; then
        echo "ERROR: $src not found. Build first: cmake --build extension/build"
        exit 1
    fi
    cp -f "$src" "$STAGE/$lib"
done

echo "==> Re-sign with Developer ID + hardened runtime + timestamp"
for lib in "${DYLIBS[@]}"; do
    codesign --force --options runtime --timestamp \
        --sign "$SIGN_IDENTITY" "$STAGE/$lib"
    codesign --verify --strict --verbose=2 "$STAGE/$lib" 2>&1 | tail -3
done

echo "==> Pack zip for notarization submission"
rm -f "$ZIP_PATH"
(cd "$STAGE" && zip -j "$ZIP_PATH" "${DYLIBS[@]}")

echo "==> Submit to Apple notary (this can take 1-10 min)"
xcrun notarytool submit "$ZIP_PATH" \
    --keychain-profile "$NOTARY_PROFILE" \
    --wait

# A "stapled" ticket can't be attached to a loose dylib (Apple only
# supports stapling on .app/.dmg/.pkg). The notarization itself is
# enough: Apple's notary cloud has the file hashes registered, so
# Gatekeeper online-checks them on first load and accepts.
#
# Verify each dylib's notarisation is actually recorded:
echo "==> Verify notarisation via spctl (the dylib variant of stapler)"
for lib in "${DYLIBS[@]}"; do
    if spctl -a -t install --raw "$STAGE/$lib" 2>&1 | tail -2; then
        echo "    $lib: accepted"
    else
        echo "    $lib: NOT accepted by Gatekeeper (review notary log)"
    fi
done

echo ""
echo "==> Done. Artifact: $ZIP_PATH"
ls -lh "$ZIP_PATH"
