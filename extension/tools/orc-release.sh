#!/usr/bin/env bash
# ORC release: build, licence notices, Developer ID signature, notarisation,
# disk image. Release plan steps 3b to 7 (docs/orc-mac-release-plan.md).
#
# Runs on the Mac only, never in CI: notarising needs the keychain profile.
#   - "Developer ID Application: Frank Acklin (234VF7874N)" in the keychain
#   - notarytool profile "rea-sixty-notarize" (the one dist/release-mac.sh uses)
#
# Run from anywhere:
#   extension/tools/orc-release.sh
#
# Output in dist/orc-<version>/:
#   ORC-<version>.dmg            signed, notarised, stapled; the app inside too
#   libusb-<version>.tar.bz2     the LGPL source of the libusb in this release
#   THIRD-PARTY-NOTICES.txt      the same notices the app and the image carry
# Nothing is published. The GitHub release is step 9.

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/extension"
BUILD="$EXT/build-orc-release"
SIGN_ID="Developer ID Application: Frank Acklin (234VF7874N)"
PROFILE="rea-sixty-notarize"

VERSION="$(sed -nE 's/.*MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+([0-9.]+).*/\1/p' "$EXT/CMakeLists.txt" | head -1)"
LIBUSB_VERSION="$(sed -nE 's/.*set\(ORC_LIBUSB_VERSION[[:space:]]+([0-9.]+)\).*/\1/p' "$EXT/CMakeLists.txt")"
LIBUSB_SHA="$(sed -nE 's/.*set\(ORC_LIBUSB_SHA256[[:space:]]+([0-9a-f]+)\).*/\1/p' "$EXT/CMakeLists.txt")"
[[ -n "$VERSION" && -n "$LIBUSB_VERSION" && -n "$LIBUSB_SHA" ]] || { echo "Cannot read versions from CMakeLists.txt"; exit 1; }

OUT="$REPO/dist/orc-$VERSION"
APP="$OUT/work/ORC.app"
echo "==> ORC $VERSION, libusb $LIBUSB_VERSION -> $OUT"

# ── 1. build ────────────────────────────────────────────────────────────────
cmake -S "$EXT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD" --target orc --parallel
ARCHS="$(lipo -archs "$BUILD/ORC.app/Contents/MacOS/ORC")"
[[ "$ARCHS" == *arm64* && "$ARCHS" == *x86_64* ]] || { echo "ORC is not universal: $ARCHS"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/work"
ditto "$BUILD/ORC.app" "$APP"

# ── 2. licence notices, in the app and beside the image ────────────────────
LIC="$APP/Contents/Resources/Licenses"
mkdir -p "$LIC"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@LIBUSB_VERSION@/$LIBUSB_VERSION/g" \
    "$EXT/orc/licenses/THIRD-PARTY-NOTICES.txt" > "$LIC/THIRD-PARTY-NOTICES.txt"
# The LGPL text and the authors from the very source that was built.
LUSRC="$BUILD/orc-libusb/src/orc_libusb"
cp "$LUSRC/COPYING" "$LIC/libusb-COPYING.txt"
cp "$LUSRC/AUTHORS" "$LIC/libusb-AUTHORS.txt"
cp "$LIC/THIRD-PARTY-NOTICES.txt" "$OUT/THIRD-PARTY-NOTICES.txt"

# The source to offer "from the same place" (LGPL-2.1 section 4), checked
# against the pinned checksum.
TARBALL="$(find "$BUILD/orc-libusb/src" -maxdepth 1 -name "libusb-$LIBUSB_VERSION.tar.bz2" | head -1)"
[[ -f "$TARBALL" ]] || { echo "libusb tarball not found under $BUILD/orc-libusb/src"; exit 1; }
[[ "$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)" == "$LIBUSB_SHA" ]] || { echo "libusb tarball checksum mismatch"; exit 1; }
cp "$TARBALL" "$OUT/libusb-$LIBUSB_VERSION.tar.bz2"

# ── 3. sign, innermost first ─────────────────────────────────────────────────
echo "==> Signing with $SIGN_ID"
codesign --force --options runtime --timestamp --sign "$SIGN_ID" \
    "$APP/Contents/Frameworks/libusb-1.0.0.dylib"
codesign --force --options runtime --timestamp --sign "$SIGN_ID" \
    --entitlements "$EXT/orc/ORC.entitlements" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

# ── 4. notarise the app and staple it, so it opens offline too ──────────────
notarise() {
    local file="$1"
    echo "==> Notarising $(basename "$file") (a few minutes)"
    local result
    result="$(xcrun notarytool submit "$file" --keychain-profile "$PROFILE" \
                  --wait --output-format json)"
    echo "$result"
    local status id
    status="$(echo "$result" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("status",""))')"
    id="$(echo "$result" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))')"
    if [[ "$status" != "Accepted" ]]; then
        echo "Notarisation: $status. Apple's log:"
        xcrun notarytool log "$id" --keychain-profile "$PROFILE" || true
        exit 1
    fi
}
( cd "$OUT/work" && ditto -c -k --keepParent ORC.app ORC.zip )
notarise "$OUT/work/ORC.zip"
xcrun stapler staple "$APP"
spctl --assess --type execute --verbose=2 "$APP"

# ── 5. the disk image ────────────────────────────────────────────────────────
DMG="$OUT/ORC-$VERSION.dmg"
ROOT="$OUT/work/dmg"
mkdir -p "$ROOT/Licenses"
ditto "$APP" "$ROOT/ORC.app"
ln -s /Applications "$ROOT/Applications"
cp "$LIC/"*.txt "$ROOT/Licenses/"
hdiutil create -volname "ORC $VERSION" -srcfolder "$ROOT" -ov -format UDZO "$DMG" > /dev/null
codesign --force --timestamp --sign "$SIGN_ID" "$DMG"
notarise "$DMG"
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG"

rm -rf "$OUT/work"
echo "==> Done:"
ls -la "$OUT"
