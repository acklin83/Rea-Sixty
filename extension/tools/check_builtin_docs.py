#!/usr/bin/env python3
"""Every registered builtin must have a line in builtinDescription().

The picker shows that line as a tooltip and searches it, so a builtin without
one is an action the user can bind and cannot look up. This walks main.cpp's
registrations, resolves each name against Bindings.cpp's doc table the same way
builtinDescription() does (exact first, then prefixes in order), and exits
non-zero on a gap.

Run from anywhere:  python3 extension/tools/check_builtin_docs.py
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"


def registered_names(main_cpp: str) -> set:
    """Names passed to registerBuiltin, including the ones built in loops."""
    # registerBuiltin plus the small helpers that register on its behalf --
    # selection modes, UF1 views and the FX-param pair never call it directly.
    names = set()
    for fn in ("registerBuiltin", "registerSelectionModeToggle",
               "regUf1View", "registerFxParamStep"):
        names.update(re.findall(fn + r'\(\s*"([a-z0-9_]+)"', main_cpp))

    # Loop-built families: snprintf(buf, ..., "name_%d", i) then
    # registerBuiltin(buf, ...). Expand the printf pattern over its real range
    # rather than guessing -- the ranges are literal in the source.
    for fmt, lo, hi in [
        (r'"switch_cs_%d"', 1, 8), (r'"copy_cs_%d"', 1, 8),
        (r'"switch_bc_%d"', 1, 8), (r'"copy_bc_%d"', 1, 8),
        (r'"switch_fav_%d"', 1, 8), (r'"copy_fav_%d"', 1, 8),
        (r'"send_all_%d"', 1, 8), (r'"recv_all_%d"', 1, 8),
        (r'"softkey_bank_%d"', 0, 5), (r'"ssl_bank_%d"', 1, 5),
        (r'"param_group_add_%d"', 1, 8), (r'"param_group_clear_%d"', 1, 8),
        (r'"param_group_toggle_%d"', 1, 8),
    ]:
        if re.search(fmt, main_cpp):
            stem = fmt.strip('"').replace('%d', '')
            names.update(f"{stem}{i}" for i in range(lo, hi + 1))
        else:
            print(f"  ! pattern {fmt} no longer in main.cpp -- update this script")
    return names


def doc_tables(bindings_cpp: str):
    """(exact set, ordered prefix list) as builtinDescription resolves them."""
    def entries(marker: str, end: str):
        blk = bindings_cpp[bindings_cpp.index(marker):bindings_cpp.index(end)]
        return re.findall(r'\{\s*"([a-z0-9_]+)"\s*,', blk)
    exact = entries("static const BuiltinDoc kBuiltinDocs[]",
                    "static const BuiltinDoc kBuiltinDocPrefixes[]")
    pref = entries("static const BuiltinDoc kBuiltinDocPrefixes[]",
                   "const char* builtinDescription")
    return set(exact), pref


def main() -> int:
    main_cpp = (SRC / "main.cpp").read_text()
    bindings = (SRC / "Bindings.cpp").read_text()

    names = registered_names(main_cpp)
    exact, prefixes = doc_tables(bindings)

    missing = [n for n in sorted(names)
               if n not in exact and not any(n.startswith(p) for p in prefixes)]

    # A doc entry for something that is not registered any more is dead weight
    # and usually means a builtin was renamed without its line following.
    stale = [n for n in sorted(exact) if n not in names]

    print(f"registered builtins : {len(names)}")
    print(f"documented (exact)  : {len(exact)}")
    print(f"documented (prefix) : {len(prefixes)}")
    if missing:
        print(f"\nMISSING a description ({len(missing)}):")
        for n in missing:
            print(f"  {n}")
    if stale:
        print(f"\nSTALE doc entries, no longer registered ({len(stale)}):")
        for n in stale:
            print(f"  {n}")
    if not missing and not stale:
        print("\nall registered builtins resolve to a description")
    return 1 if (missing or stale) else 0


if __name__ == "__main__":
    sys.exit(main())
