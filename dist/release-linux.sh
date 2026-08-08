#!/usr/bin/env bash
# Rea-Sixty Linux release packer. Bundles the built .so plus the libusb /
# hidapi it depends on, for upload to the GitHub Release that the ReaPack
# index.xml points at.
#
# The CMake build (extension/CMakeLists.txt, "Linux libusb / hidapi
# bundling") copies the system libusb / hidapi next to the .so under their
# SONAME and gives the .so an $ORIGIN RUNPATH. We ship all three so the
# plugin is self-contained: no `apt install libhidapi-hidraw0` for the user.
# That package is almost never preinstalled on minimal distros, and without
# it REAPER's dlopen fails and the plugin is silently dropped — installed via
# ReaPack yet absent from Control/OSC/Web (Frank forum support, 2026-08-04).
#
# Includes the udev rule that's mandatory for non-root USB access —
# ReaPack's @provides will install it next to the .so (user copies
# manually with sudo).
#
# Pre-reqs:
#   - extension/build/reaper_rea-sixty.so built on the target box, with the
#     bundled libusb-1.0.so.* / libhidapi-*.so.* sitting beside it (the CMake
#     POST_BUILD step drops them there).
#
# Run:
#   ./dist/release-linux.sh
#
# Output: dist/rea-sixty-linux-v<VERSION>.tar.gz with the .so + bundled libs
# + udev rule + INSTALL.txt.

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

# Bundled runtime libs the CMake POST_BUILD step dropped next to the .so,
# named by their SONAME (= the .so's DT_NEEDED). Ship them so the plugin
# loads without the user apt-installing anything. Fail loud if the build
# didn't produce them — a tarball missing them silently reintroduces the
# "installed but invisible" bug.
BUNDLED_LIBS=()
shopt -s nullglob
# Patterns stay LITERAL here (single-quoted); they expand against $BUILD_DIR
# below, not against the current directory.
for pat in 'libusb-1.0.so.*' 'libhidapi-*.so.*'; do
    for lib in "$BUILD_DIR"/$pat; do
        cp -f "$lib" "$STAGE/$(basename "$lib")"
        BUNDLED_LIBS+=("$(basename "$lib")")
    done
done
shopt -u nullglob
if [[ ${#BUNDLED_LIBS[@]} -lt 2 ]]; then
    echo "ERROR: expected bundled libusb + hidapi next to $SRC, found: ${BUNDLED_LIBS[*]:-none}"
    echo "       Rebuild so the CMake POST_BUILD 'Bundling ...' step runs:"
    echo "         cmake --build extension/build"
    exit 1
fi
echo "==> Bundled libs: ${BUNDLED_LIBS[*]}"

# The .so and the bundled libs must not demand a newer glibc / libstdc++ than
# the oldest distro we support. Nothing about this shows up on the build box —
# it only breaks at the user's dlopen ("version `GLIBCXX_3.4.31' not found"),
# which is how v0.4.4 shipped unloadable on MX Linux 23 and Debian 12. Checked
# here because this script also runs on the Mac against CI artifacts, where a
# wrong build image would otherwise pass unnoticed.
CHECK_ABI="$REPO_ROOT/dist/check-linux-abi.py"
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found — needed for the ABI floor check ($CHECK_ABI)."
    echo "       Install python3; do not ship an unverified Linux package."
    exit 1
fi
python3 "$CHECK_ABI" "$STAGE/reaper_rea-sixty.so" \
    "${BUNDLED_LIBS[@]/#/$STAGE/}"

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

1. Copy reaper_rea-sixty.so AND the bundled libraries
   (${BUNDLED_LIBS[*]}) to ~/.config/REAPER/UserPlugins/
   — keep them together; the .so loads them from its own directory.
2. As root: copy 99-rea-sixty.rules to /etc/udev/rules.d/, then run
       sudo udevadm control --reload-rules && sudo udevadm trigger
3. Install ReaImGui from ReaPack inside REAPER (Extensions → ReaPack
   → Browse packages → ReaImGui → Install)
4. Restart REAPER, then Preferences → Control/OSC/Web → Add → Rea-Sixty

libusb / hidapi are bundled — no apt install needed. (They still rely on
your system's libudev, present on every desktop Linux.)

Requires glibc 2.34 or newer: Ubuntu 22.04+, Debian 12+, MX Linux 23+,
Fedora 35+. On anything older, build from source.

Known issue: USB stability depends on topology. Linux kernel (xhci_hcd)
can power-cycle a USB hub port that has UF8 + UC1 daisy-chained on it,
producing "disabled by hub (EMI?), re-enabling..." in dmesg. Plug UF8
and UC1 into SEPARATE PC USB ports for a stable session.
EOF

echo "==> Pack tar.gz"
rm -f "$TGZ_PATH"
tar -C "$STAGE" -czf "$TGZ_PATH" \
    reaper_rea-sixty.so "${BUNDLED_LIBS[@]}" 99-rea-sixty.rules INSTALL.txt

echo ""
echo "==> Done. Artifact: $TGZ_PATH"
ls -lh "$TGZ_PATH"
