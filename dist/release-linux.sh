#!/usr/bin/env bash
# Rea-Sixty Linux release packer. Bundles the built .so for upload to
# the GitHub Release that the ReaPack index.xml points at.
#
# Unlike macOS, Linux doesn't bundle libusb / hidapi into the package —
# users get them from their distro's package manager (Debian/Ubuntu:
# libusb-1.0-0, libhidapi-hidraw0). Their ABI is stable enough that
# system libs work; bundling versioned .so's with rpath rewrites is
# unnecessary churn for the v0.1.x cycle.
#
# Includes the udev rule that's mandatory for non-root USB access —
# ReaPack's @provides will install it next to the .so (user copies
# manually with sudo).
#
# Pre-reqs:
#   - extension/build/reaper_rea-sixty.so built on the target box
#
# Run:
#   ./dist/release-linux.sh
#
# Output: dist/rea-sixty-linux-v<VERSION>.tar.gz with the .so + udev
# rule + README explaining the manual steps.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/extension/build"
DIST_DIR="$REPO_ROOT/dist"

VERSION="$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null \
            || git -C "$REPO_ROOT" rev-parse --short HEAD)"

STAGE="$DIST_DIR/stage-linux-$VERSION"
TGZ_PATH="$DIST_DIR/rea-sixty-linux-$VERSION.tar.gz"

echo "==> Release stage: $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"

SRC="$BUILD_DIR/reaper_rea-sixty.so"
if [[ ! -f "$SRC" ]]; then
    echo "ERROR: $SRC not found. Build first: cmake --build extension/build"
    exit 1
fi
cp -f "$SRC" "$STAGE/reaper_rea-sixty.so"

# udev rule — same content the developer installs manually. End users
# must root-copy this to /etc/udev/rules.d/ for libusb to talk to UF8
# and UC1 without sudo.
cat > "$STAGE/99-rea-sixty.rules" <<'RULES'
# Solid State Logic UF8 / UC1 — libusb + hidraw access for Rea-Sixty.
# Copy to /etc/udev/rules.d/ and run:
#   sudo udevadm control --reload-rules && sudo udevadm trigger
SUBSYSTEM=="usb",    ATTRS{idVendor}=="31e9", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="31e9", MODE="0666", TAG+="uaccess"
RULES

cat > "$STAGE/INSTALL.txt" <<EOF
Rea-Sixty for Linux — manual install

1. Copy reaper_rea-sixty.so to ~/.config/REAPER/UserPlugins/
2. As root: copy 99-rea-sixty.rules to /etc/udev/rules.d/, then run
       sudo udevadm control --reload-rules && sudo udevadm trigger
3. Install dependencies (Debian/Ubuntu):
       sudo apt install libusb-1.0-0 libhidapi-hidraw0
4. Install ReaImGui from ReaPack inside REAPER (Extensions → ReaPack
   → Browse packages → ReaImGui → Install)
5. Restart REAPER, then Preferences → Control/OSC/Web → Add → Rea-Sixty

Known issue: USB stability depends on topology. Linux kernel (xhci_hcd)
can power-cycle a USB hub port that has UF8 + UC1 daisy-chained on it,
producing "disabled by hub (EMI?), re-enabling..." in dmesg. Plug UF8
and UC1 into SEPARATE PC USB ports for a stable session.
EOF

echo "==> Pack tar.gz"
rm -f "$TGZ_PATH"
tar -C "$STAGE" -czf "$TGZ_PATH" \
    reaper_rea-sixty.so 99-rea-sixty.rules INSTALL.txt

echo ""
echo "==> Done. Artifact: $TGZ_PATH"
ls -lh "$TGZ_PATH"
