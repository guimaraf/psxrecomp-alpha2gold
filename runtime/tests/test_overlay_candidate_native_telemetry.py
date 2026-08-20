#!/usr/bin/env python3
"""Structural regression for per-candidate native execution telemetry.

Usage: python runtime/tests/test_overlay_candidate_native_telemetry.py
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


def require(pattern: str, text: str, message: str) -> None:
    if not re.search(pattern, text, re.S):
        raise AssertionError(message)


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    candidate = re.search(r"typedef\s+struct\s*\{(.*?)\}\s*Candidate\s*;", source, re.S)
    if not candidate:
        raise AssertionError("missing Candidate structure")
    require(r"uint64_t\s+native_entries\s*;", candidate.group(1),
            "Candidate is missing native_entries")
    require(r"uint64_t\s+native_continuations\s*;", candidate.group(1),
            "Candidate is missing native_continuations")

    for name in ("cand_register", "register_sljit_candidate"):
        body = function_body(source, name)
        require(r"c->native_entries\s*=\s*0\s*;", body,
                f"{name} does not reset native_entries")
        require(r"c->native_continuations\s*=\s*0\s*;", body,
                f"{name} does not reset native_continuations")

    dispatch = function_body(source, "overlay_loader_dispatch")
    require(
        r"if\s*\(\s*!s_in_shadow\s*\)\s*"
        r"c->native_continuations\+\+\s*;\s*s_native_calls_total\+\+\s*;",
        dispatch,
        "CPS candidate counter is not explicitly restricted to live execution",
    )
    require(
        r"if\s*\(\s*!s_in_shadow\s*\)\s*"
        r"c->native_entries\+\+\s*;\s*s_native_calls_total\+\+\s*;",
        dispatch,
        "entry candidate counter is not explicitly restricted to live execution",
    )
    if dispatch.count("c->native_entries++;") != 1:
        raise AssertionError("native_entries must have exactly one live increment site")
    if dispatch.count("c->native_continuations++;") != 1:
        raise AssertionError("native_continuations must have exactly one live increment site")

    shadow = function_body(source, "run_shadow_diff")
    if "native_entries" in shadow or "native_continuations" in shadow:
        raise AssertionError("shadow differential execution pollutes live native counters")

    for name in ("overlay_loader_dump_candidates", "overlay_loader_dump_candidates_at"):
        body = function_body(source, name)
        require(r'\\"native_entries\\":%llu', body,
                f"{name} does not expose native_entries")
        require(r'\\"native_continuations\\":%llu', body,
                f"{name} does not expose native_continuations")

    print("PASS: candidate telemetry counts live entries/continuations and excludes shadow")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
