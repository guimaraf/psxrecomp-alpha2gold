#!/usr/bin/env python3
"""Structural regression test for CPU-written main-text overlay capture.

An ordinary guest CPU store can install code over the boot EXE without setting
the CD-DMA dirty bitmap. Exact static-range validation records the mismatch in
the text-divergence bitmap. That evidence must feed capture, cache candidacy,
local interpreter flow, and the optional JIT tier consistently.

Usage: python runtime/tests/test_overlay_cpu_written_text_capture.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "runtime" / "include" / "dirty_ram_interp.h"
MEMORY = ROOT / "runtime" / "src" / "memory.c"
CAPTURE = ROOT / "runtime" / "src" / "overlay_capture.c"
INTERP = ROOT / "runtime" / "src" / "dirty_ram_interp.c"
LOADER = ROOT / "runtime" / "src" / "overlay_loader.c"


def require(pattern: str, source: str, message: str) -> re.Match[str]:
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError(message)
    return match


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    memory = MEMORY.read_text(encoding="utf-8")
    capture = CAPTURE.read_text(encoding="utf-8")
    interp = INTERP.read_text(encoding="utf-8")
    loader = LOADER.read_text(encoding="utf-8")

    require(
        r"dirty_ram_code_is_dynamic\s*\([^)]*\)\s*\{[^}]*"
        r"dirty_ram_is_dirty\s*\([^)]*\)\s*\|\|\s*"
        r"dirty_ram_text_page_diverged\s*\(",
        header,
        "dynamic-code predicate must include DMA dirty and text divergence",
    )
    require(
        r"overlay_cache_window_contains\s*\([^)]*\)\s*\{[^}]*"
        r"dirty_ram_code_is_dynamic\s*\(",
        header,
        "overlay cache window ignores CPU-written divergent text",
    )
    require(
        r"int\s+dirty_ram_text_page_diverged\s*\([^)]*\)\s*\{[^}]*"
        r"text_diverged_bitmap",
        memory,
        "memory layer does not expose the proven text-divergence page bit",
    )
    require(
        r"bitmap\[i\]\s*=\s*dirty_ram_get_bitmap_word\s*\(i\)\s*"
        r"\|\s*dirty_ram_text_diverged_bitmap_word\s*\(i\)",
        capture,
        "capture snapshot must union DMA dirty and text-divergence bitmaps",
    )

    required_interp_sites = (
        r"is_local_dirty_target\s*\([^)]*\)\s*\{[^}]*dirty_ram_code_is_dynamic",
        r"precise_pc_dispatchable\s*\([^)]*\)\s*\{[^}]*dirty_ram_code_is_dynamic",
        r"allow_local_dirty_flow[^;]*;.*?dirty_ram_code_is_dynamic\s*\(target_phys\)",
        r"if\s*\(!dirty_ram_code_is_dynamic\s*\(next_phys\)\)",
    )
    for pattern in required_interp_sites:
        require(pattern, interp, "interpreter flow still excludes divergent text code")

    if len(re.findall(r"dirty_ram_code_is_dynamic\s*\(phys\)", loader)) < 2:
        raise AssertionError("optional JIT gates do not accept CPU-installed code")

    print("PASS: CPU-written divergent text feeds capture and native cache tiers")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
