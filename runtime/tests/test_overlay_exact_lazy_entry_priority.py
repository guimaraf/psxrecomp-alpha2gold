#!/usr/bin/env python3
"""Structural regression for exact lazy entries masked by CPS range owners.

Usage: python runtime/tests/test_overlay_exact_lazy_entry_priority.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "runtime" / "src" / "overlay_loader.c"


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"\b(?:int|void)\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{",
        source,
        re.S,
    )
    if not match:
        raise AssertionError(f"missing function definition: {name}")

    start = match.end()
    depth = 1
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos]
    raise AssertionError(f"unterminated function definition: {name}")


def require(pattern: str, text: str, message: str) -> re.Match[str]:
    match = re.search(pattern, text, re.S)
    if not match:
        raise AssertionError(message)
    return match


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    dispatch = function_body(source, "overlay_loader_dispatch")

    lazy_exact = require(
        r"lazy_exact\s*=\s*head\s*<\s*0\s*&&\s*lazy_has_exact_entry\s*\(phys\)\s*;",
        dispatch,
        "dispatch no longer identifies exact entries in the lazy manifest index",
    )
    exact_priority = require(
        r"exact_needs_load\s*=\s*lazy_exact\s*;",
        dispatch,
        "a loaded CPS range owner can still mask an exact lazy entry",
    )
    load_gate = require(
        r"if\s*\(\s*head\s*<\s*0\s*&&\s*"
        r"\(loaded_range_ci\s*<\s*0\s*\|\|\s*exact_needs_load\)\s*&&\s*"
        r"!lazy_loaded\s*&&\s*try_load_region\s*\(phys\)\s*\)",
        dispatch,
        "exact lazy entries cannot trigger bundle loading over an enclosing range owner",
    )
    range_lookup = require(
        r"loaded_range_ci\s*=\s*overlay_find_by_range\s*\(phys\)\s*;",
        dispatch,
        "ordinary CPS continuations lost their range-owner lookup",
    )

    if not (lazy_exact.start() < range_lookup.start() < exact_priority.start() < load_gate.start()):
        raise AssertionError(
            "lazy exact detection, range lookup, priority decision and load gate are misordered"
        )

    if re.search(r"exact_needs_load\s*=.*device_touch", dispatch):
        raise AssertionError(
            "device_touch still controls whether an exact manifested entry is published"
        )

    selector = function_body(source, "try_load_region")
    require(
        r"lazy_bundle_matches\s*\(ci\)\s*&&\s*"
        r"\(best\s*<\s*0\s*\|\|\s*s_cache_idx\[ci\]\.func_count\s*>\s*"
        r"s_cache_idx\[s_lazy_man\[best\]\.cache_idx\]\.func_count\)",
        selector,
        "coherent bundle selection no longer prefers the more complete manifest",
    )

    print("PASS: exact lazy entries outrank loaded CPS range owners")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
