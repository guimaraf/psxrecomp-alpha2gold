#!/usr/bin/env python3
"""Structural regression for stale exact entries masking CPS range owners.

Usage: python runtime/tests/test_overlay_cps_stale_exact_fallback.py
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

    cps_gate = require(
        r"if\s*\(\s*\(head\s*<\s*0\s*\|\|\s*exact_chain_exhausted\)\s*&&\s*"
        r"g_psx_cps_mode\s*\)",
        dispatch,
        "CPS range lookup is still restricted to PCs without exact candidates",
    )
    exact_loop = require(
        r"for\s*\(\s*int\s+i\s*=\s*head\s*;\s*i\s*>=\s*0\s*;\s*"
        r"i\s*=\s*s_cand\[i\]\.next\s*\)",
        dispatch,
        "missing exact-candidate priority chain",
    )
    retry = require(
        r"if\s*\(\s*head\s*>=\s*0\s*&&\s*g_psx_cps_mode\s*\)\s*\{\s*"
        r"exact_chain_exhausted\s*=\s*1\s*;\s*goto\s+try_cps_range\s*;\s*\}",
        dispatch,
        "exhausted exact candidates do not retry the CPS range owner",
    )
    skip = require(
        r"if\s*\(\s*exact_chain_exhausted\s*\)\s*"
        r"goto\s+after_exact_candidates\s*;",
        dispatch,
        "post-exact CPS retry can loop back through the exhausted exact chain",
    )

    if not (cps_gate.start() < skip.start() < exact_loop.start() < retry.start()):
        raise AssertionError(
            "dispatch ordering no longer preserves exact-live priority followed by CPS fallback"
        )

    range_owner = function_body(source, "overlay_find_by_range")
    require(
        r"range_candidate_matches\s*\(",
        range_owner,
        "CPS fallback does not validate range-owner variants",
    )
    matcher = function_body(source, "range_candidate_matches")
    require(
        r"ENTRY_INVALID\s*&&\s*gen\s*==\s*c->val_gen\s*\)\s*return\s+0\s*;",
        matcher,
        "known-stale range owners are not rejected before selection",
    )

    print("PASS: stale exact entries fall through to a validated CPS range owner")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
