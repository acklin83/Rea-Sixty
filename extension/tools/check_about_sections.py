#!/usr/bin/env python3
"""The About page's sections for every user stay outside the dev_probes gate.

From 27.09. (2488f64) until 30.09.2026 the closing brace of the dev_probes
block sat at the end of drawAbout instead of right after the UF1 display probe
it was meant to hide. Every Windows and Linux user lost the WinUSB installer
and the udev rule, with their Uninstall buttons, plus Logs and
Acknowledgements. Nobody saw it until users asked in the forum and in issue #9,
because on the developer's machine dev_probes is set.

This finds drawAbout, the if that reads dev_probes, and that block's matching
brace (skipping strings, character literals and comments), and fails when a
section heading below is missing or sits inside the block.

Run from anywhere:  python3 extension/tools/check_about_sections.py
"""
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src" / "SettingsScreen.cpp"

# The headings that belong to everybody.
SECTIONS = [
    '"Windows USB driver"',
    '"Linux udev rule"',
    '"Logs"',
    '"Acknowledgements"',
]


def match_brace(text: str, open_at: int) -> int:
    """Index of the brace closing the one at open_at, or -1."""
    depth = 0
    i, n = open_at, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                return -1
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i + 2)
            if i < 0:
                return -1
            i += 2
            continue
        if c in "\"'":
            q, i = c, i + 1
            while i < n and text[i] != q:
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    start = text.find("void SettingsScreen::drawAbout(")
    if start < 0:
        print("check_about_sections: drawAbout not found")
        return 1
    body_open = text.find("{", start)
    body_close = match_brace(text, body_open)
    gate = text.find('"dev_probes"', body_open, body_close)
    if gate < 0:
        print("check_about_sections: no dev_probes gate in drawAbout; nothing to check")
        return 0
    gate_open = text.find("{", gate)
    gate_close = match_brace(text, gate_open)
    if gate_close < 0 or gate_close > body_close:
        print("check_about_sections: cannot find the end of the dev_probes block")
        return 1

    bad = []
    for heading in SECTIONS:
        at = text.find(heading, body_open, body_close)
        if at < 0:
            bad.append(f"{heading} is not in drawAbout")
        elif gate_open < at < gate_close:
            line = text.count("\n", 0, at) + 1
            bad.append(f"{heading} (line {line}) is inside the dev_probes block, "
                       "so only a developer sees it")
    if bad:
        for b in bad:
            print("check_about_sections: " + b)
        return 1
    print("check_about_sections: every About section is outside the dev_probes gate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
