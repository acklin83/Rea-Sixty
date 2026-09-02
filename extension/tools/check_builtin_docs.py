#!/usr/bin/env python3
"""Every registered builtin must have a description AND a soft-key label.

Two tables, two failure modes. builtinDescription() feeds the picker's tooltip
and its search, so a builtin without one can be bound and not looked up.
builtinShortLabel() feeds the Label field when an action is assigned, so a
builtin without one ships a nameless key — the label stays blank and the
scribble says nothing about what the key does.

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
        # softkey_bank_N is 1..9, one per (layer, Quick) — the 0..5 here was the
        # RANGE OF A DIFFERENT LOOP (the bank selectors' param), so the checker
        # was asking about a softkey_bank_0 that does not exist and never asked
        # about 6 to 9 (found 2026-09-02 while filling the label table).
        (r'"softkey_bank_%d"', 1, 9), (r'"ssl_bank_%d"', 1, 5),
        (r'"param_group_add_%d"', 1, 8), (r'"param_group_clear_%d"', 1, 8),
        (r'"param_group_toggle_%d"', 1, 8),
    ]:
        if re.search(fmt, main_cpp):
            stem = fmt.strip('"').replace('%d', '')
            names.update(f"{stem}{i}" for i in range(lo, hi + 1))
        else:
            print(f"  ! pattern {fmt} no longer in main.cpp -- update this script")
    return names


def label_table(bindings_cpp: str):
    """{name: label} from kBuiltinLabels, the soft-key default names."""
    blk = bindings_cpp[bindings_cpp.index("static const BuiltinLabel kBuiltinLabels[]"):
                       bindings_cpp.index("const char* builtinShortLabel")]
    return dict(re.findall(r'\{\s*"([a-z0-9_]+)"\s*,\s*"([^"]*)"\s*\}', blk))


def doc_tables(bindings_cpp: str):
    """(exact set, ordered prefix list) as builtinDescription resolves them."""
    def entries(marker: str, end: str):
        blk = bindings_cpp[bindings_cpp.index(marker):bindings_cpp.index(end)]
        return re.findall(r'\{\s*"([a-z0-9_]+)"\s*,', blk)
    exact = entries("static const BuiltinDoc kBuiltinDocs[]",
                    "static const BuiltinDoc kBuiltinDocPrefixes[]")
    # ⚠ END AT THE LABEL TABLE, NOT AT builtinDescription. kBuiltinLabels was
    # added between them on 2026-09-02, and reading to the function swallowed all
    # 321 labels as prefix entries — after which every builtin "resolved" and
    # this check reported success on anything. Found because softkey_set_engage
    # has no description and was passing (Frank: the picker tooltip for
    # "Soft-Key Set 1" talks about banks).
    pref = entries("static const BuiltinDoc kBuiltinDocPrefixes[]",
                   "// Default soft-key label per builtin")
    return set(exact), pref


def main() -> int:
    main_cpp = (SRC / "main.cpp").read_text()
    bindings = (SRC / "Bindings.cpp").read_text()

    names = registered_names(main_cpp)
    exact, prefixes = doc_tables(bindings)
    labels = label_table(bindings)

    missing = [n for n in sorted(names)
               if n not in exact and not any(n.startswith(p) for p in prefixes)]

    # A doc entry for something that is not registered any more is dead weight
    # and usually means a builtin was renamed without its line following.
    stale = [n for n in sorted(exact) if n not in names]

    # ⇨ THE LABEL IS A HARD CAP, NOT A SUGGESTION. Twelve characters is what the
    # surface shows; a longer one arrives cut mid-word, which is worse than the
    # blank it replaced.
    # Internal sentinels (leading "__") never reach the picker — builtinNames()
    # filters them — so they need no label either.
    no_label   = [n for n in sorted(names)
                  if n not in labels and not n.startswith("__")]
    long_label = sorted((n, l) for n, l in labels.items() if len(l) > 12)
    stale_lbl  = [n for n in sorted(labels) if n not in names]

    print(f"registered builtins : {len(names)}")
    print(f"soft-key labels     : {len(labels)}")
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
    if no_label:
        print(f"\nMISSING a soft-key label ({len(no_label)}):")
        for n in no_label:
            print(f"  {n}")
    if long_label:
        print(f"\nLABEL OVER 12 CHARACTERS ({len(long_label)}):")
        for n, l in long_label:
            print(f"  {n}  ->  {l!r} ({len(l)})")
    if stale_lbl:
        print(f"\nSTALE label entries, no longer registered ({len(stale_lbl)}):")
        for n in stale_lbl:
            print(f"  {n}")
    bad = bool(missing or stale or no_label or long_label or stale_lbl)
    if not bad:
        print("\nevery registered builtin has a description and a label \u226412 chars")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
