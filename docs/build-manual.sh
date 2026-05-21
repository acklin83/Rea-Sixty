#!/usr/bin/env bash
# Build docs/user-manual.md → user-manual.pdf via pandoc.
#
# Pre-reqs:
#   - pandoc (brew install pandoc / sudo apt install pandoc)
#   - A LaTeX engine. Recommended on macOS:
#       brew install --cask basictex
#       eval "$(/usr/libexec/path_helper)"
#       sudo tlmgr update --self && sudo tlmgr install collection-fontsrecommended
#     Lightweight alternative (rust-based, no apt/brew):
#       cargo install tectonic
#
# Output: docs/user-manual.pdf

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/docs/user-manual.md"
OUT="$REPO_ROOT/docs/user-manual.pdf"

if ! command -v pandoc >/dev/null 2>&1; then
    echo "ERROR: pandoc not found in PATH."
    echo "Install: brew install pandoc  (or: sudo apt install pandoc)"
    exit 1
fi

# Prefer tectonic when available (self-contained, no system LaTeX install).
# Fall back to xelatex (BasicTeX / MacTeX / TeX Live).
ENGINE=""
if command -v tectonic >/dev/null 2>&1; then
    ENGINE="--pdf-engine=tectonic"
elif command -v xelatex >/dev/null 2>&1; then
    ENGINE="--pdf-engine=xelatex"
else
    echo "ERROR: no LaTeX engine found. Install tectonic or xelatex."
    echo "macOS: brew install --cask basictex   (then re-source PATH)"
    echo "       OR  cargo install tectonic"
    echo "Linux: sudo apt install texlive-xetex texlive-fonts-recommended"
    exit 1
fi

echo "==> pandoc $ENGINE → $OUT"
pandoc "$SRC" -o "$OUT" \
    --toc \
    --toc-depth=3 \
    $ENGINE \
    -V mainfont="Lato" \
    -V sansfont="Lato" \
    -V monofont="Menlo" \
    -V linkcolor="blue" \
    -V urlcolor="blue" \
    -V toccolor="black"

echo ""
echo "==> Done."
ls -lh "$OUT"
