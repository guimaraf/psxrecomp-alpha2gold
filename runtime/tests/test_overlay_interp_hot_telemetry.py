#!/usr/bin/env python3
"""Structural regression test for hot-first interpreted-overlay telemetry.

The command must expose actionable external interpreter entries without
resetting counters, logging to disk, or returning hash-table order.

Usage: python runtime/tests/test_overlay_interp_hot_telemetry.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "runtime" / "src" / "debug_server.c"
DOCS = ROOT / "TCP_COMMANDS.md"


def require(pattern: str, source: str, message: str) -> re.Match[str]:
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError(message)
    return match


def function_body(source: str, name: str) -> str:
    match = require(
        rf"\b(?:static\s+)?(?:void|int|uint64_t|const\s+char\s*\*)\s+"
        rf"{re.escape(name)}\s*\([^;]*?\)\s*\{{",
        source,
        f"missing function definition: {name}",
    )
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


def main() -> int:
    server = SERVER.read_text(encoding="utf-8")
    docs = DOCS.read_text(encoding="utf-8")

    require(
        r"typedef\s+struct\s*\{.*?uint32_t\s+pc\s*;.*?"
        r"uint64_t\s+selected\s*;.*?uint64_t\s+hits\s*;.*?"
        r"uint64_t\s+insns\s*;.*?uint64_t\s+entry_hits\s*;.*?"
        r"\}\s*OverlayInterpHotRow\s*;",
        server,
        "overlay hot rows do not preserve all selection evidence",
    )

    parser = function_body(server, "overlay_interp_hot_parse_sort")
    selector = function_body(server, "overlay_interp_hot_selected")
    comparator = function_body(server, "overlay_interp_hot_row_compare")
    handler = function_body(server, "handle_overlay_interp_hot")

    for name, enum_name, field in (
        ("insns", "INSNS", "insns"),
        ("entries", "ENTRIES", "entry_hits"),
        ("hits", "HITS", "hits"),
    ):
        require(
            rf'strcmp\s*\(name,\s*"{name}"\)\s*==\s*0.*?'
            rf"OVERLAY_INTERP_HOT_SORT_{enum_name}",
            parser,
            f"missing overlay hot sort: {name}",
        )
        require(
            rf"OVERLAY_INTERP_HOT_SORT_{enum_name}\s*:\s*return\s+entry->{field}",
            selector,
            f"sort {name} is not backed by {field}",
        )

    require(
        r"a->selected\s*<\s*b->selected.*?a->selected\s*>\s*b->selected.*?"
        r"a->entry_hits\s*<\s*b->entry_hits.*?a->pc\s*<\s*b->pc",
        comparator,
        "overlay hot ordering is not deterministic and hot-first",
    )
    require(
        r'json_get_int\s*\(json,\s*"min_entries",\s*1\).*?'
        r'json_get_str\s*\(json,\s*"phys_lo".*?'
        r'json_get_str\s*\(json,\s*"phys_hi".*?'
        r'json_get_str\s*\(json,\s*"sort"',
        handler,
        "overlay hot query does not expose its bounded filters",
    )
    require(
        r"g_dirty_ram_pc_table.*?entry->hits\s*==\s*0.*?"
        r"overlay_cache_window_contains\s*\(phys\).*?"
        r"entry->entry_hits\s*==\s*0.*?"
        r"entry->entry_hits\s*>=\s*\(uint64_t\)min_entries",
        handler,
        "overlay hot rows are not restricted to actionable external entries",
    )
    require(
        r"window_pcs\+\+.*?window_hits\s*\+=\s*entry->hits.*?"
        r"window_insns\s*\+=\s*entry->insns.*?"
        r"external_entries\s*\+=\s*entry->entry_hits",
        handler,
        "overlay hot snapshots omit cumulative window totals",
    )
    require(
        r"qsort\s*\(rows,\s*total,\s*sizeof\(\*rows\),\s*"
        r"overlay_interp_hot_row_compare\s*\)",
        handler,
        "overlay hot rows are still emitted in hash-table order",
    )
    require(
        r"if\s*\(limit\s*>\s*256\)\s*limit\s*=\s*256.*?"
        r"begin\s*=.*?offset.*?end\s*=\s*begin\s*\+\s*\(size_t\)limit",
        handler,
        "overlay hot response is not bounded and paginated",
    )
    for field in (
        "sort",
        "phys_lo",
        "phys_hi",
        "window_pcs",
        "external_pcs",
        "window_hits",
        "window_insns",
        "external_entries",
        "selected",
        "entry_hits",
    ):
        if f'\\"{field}\\"' not in handler:
            raise AssertionError(f"overlay hot response omits {field}")
    if re.search(r"\b(?:fopen|fprintf|fwrite|memset)\s*\(", handler):
        raise AssertionError("overlay hot telemetry writes or resets external state")
    require(
        r'\{\s*"overlay_interp_hot"\s*,\s*handle_overlay_interp_hot\s*\}',
        server,
        "overlay_interp_hot command is not registered",
    )
    require(
        r"`overlay_interp_hot`.*?min_entries.*?phys_lo.*?phys_hi.*?"
        r"sort.*?insns.*?entries.*?hits",
        docs,
        "TCP command documentation is missing the overlay hot contract",
    )

    print("PASS: interpreted overlay entries are external-only, paged, and hot-first")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
