#!/usr/bin/env bash
# ORC under Windows: fetches the installer that .github/workflows/orc-windows.yml built
# from this commit into dist/orc-<version>/, next to what orc-release.sh made for the
# Mac. Both go into one GitHub release, from one commit. Nothing is published here.
#
# Run from anywhere:
#   extension/tools/orc-release-win.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -nE 's/.*MACOSX_BUNDLE_SHORT_VERSION_STRING[[:space:]]+([0-9.]+).*/\1/p' "$REPO/extension/CMakeLists.txt" | head -1)"
SHA="$(git -C "$REPO" rev-parse HEAD)"
OUT="$REPO/dist/orc-$VERSION"

RUN="$(gh run list --workflow orc-windows.yml --commit "$SHA" --status success --limit 1 --json databaseId -q '.[0].databaseId')"
[[ -n "$RUN" ]] || { echo "No green ORC Windows run for $SHA yet."; exit 1; }

mkdir -p "$OUT"
gh run download "$RUN" --name orc-windows-setup --dir "$OUT"
ls -la "$OUT"/ORC-*-Setup.exe
