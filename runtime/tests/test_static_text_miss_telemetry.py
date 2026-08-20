#!/usr/bin/env python3
"""Structural regression test for original game-text miss telemetry.

Static EXE dispatch gaps must be distinguishable from genuine runtime-written
code. The runtime exposes that evidence through the bounded TCP debug protocol;
it must not create a trace/log file.

Usage: python runtime/tests/test_static_text_miss_telemetry.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "runtime" / "include" / "dirty_ram_interp.h"
INTERP = ROOT / "runtime" / "src" / "dirty_ram_interp.c"
SERVER = ROOT / "runtime" / "src" / "debug_server.c"
MEMORY = ROOT / "runtime" / "src" / "memory.c"


def require(pattern: str, source: str, message: str) -> re.Match[str]:
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError(message)
    return match


def function_body(source: str, name: str) -> str:
    match = require(
        rf"\b(?:static\s+)?(?:void|int|DirtyRamTextDispatchClass)\s+{re.escape(name)}\s*"
        rf"\([^;]*?\)\s*\{{",
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
    header = HEADER.read_text(encoding="utf-8")
    interp = INTERP.read_text(encoding="utf-8")
    server = SERVER.read_text(encoding="utf-8")
    memory = MEMORY.read_text(encoding="utf-8")

    require(
        r"#define\s+STATIC_TEXT_MISS_TABLE_SIZE\s+32768.*?"
        r"typedef\s+struct\s*\{.*?uint64_t\s+misses\s*;.*?"
        r"uint64_t\s+modified\s*;.*?uint64_t\s+runtime\s*;.*?"
        r"uint64_t\s+unknown\s*;.*?"
        r"\}\s*StaticTextMissEntry\s*;",
        header,
        "classified static-text evidence has no bounded dedicated table",
    )
    classifier = function_body(memory, "dirty_ram_text_dispatch_classify")
    require(
        r"dirty_ram_bitmap.*?DIRTY_RAM_TEXT_DISPATCH_RUNTIME",
        classifier,
        "post-baseline executable loads are not classified as runtime code",
    )
    require(
        r"memcmp\s*\(.*?DIRTY_RAM_TEXT_DISPATCH_RUNTIME",
        classifier,
        "changed entry instructions are not classified as runtime code",
    )
    require(
        r"text_modified_bitmap.*?text_diverged_bitmap.*?"
        r"DIRTY_RAM_TEXT_DISPATCH_MODIFIED",
        classifier,
        "touched but entry-matching pages are not kept ambiguous",
    )
    if "dirty_ram_text_native_ok" in classifier:
        raise AssertionError("missing-entry classifier reuses destructive native validation")

    dispatch = function_body(interp, "dirty_ram_dispatch_inner")
    require(
        r"game_text_native_entry\s*=.*?psx_game_is_function_entry\s*\(addr\).*?"
        r"if\s*\(game_text_addr\s*&&\s*!game_text_native_entry\).*?"
        r"static_game_text_miss\s*=\s*1\s*;.*?"
        r"dirty_ram_text_dispatch_classify\s*\(phys\).*?"
        r"if\s*\(game_text_native_entry\s*&&\s*"
        r"psx_game_text_native_ok\s*\(addr\)\)",
        dispatch,
        "missing entries are not classified before the native-validity gate",
    )
    if dispatch.find("dirty_ram_text_dispatch_classify(phys)") > dispatch.find(
        "psx_game_text_native_ok(addr)"
    ):
        raise AssertionError("native-validity gate still hides missing-entry evidence")
    require(
        r"int\s+external_entry\s*=\s*addr\s*!=\s*g_dirty_interp_chain_target\s*;.*?"
        r"if\s*\(pc_entry\s*&&\s*external_entry\)\s*pc_entry->entry_hits\+\+\s*;.*?"
        r"if\s*\(external_entry\s*&&\s*static_game_text_miss\)\s*"
        r"static_text_miss_record\s*\(phys,\s*static_game_text_miss_class\)\s*;",
        dispatch,
        "static miss evidence is not restricted to external interpreter entries",
    )
    if dispatch.count("static_text_miss_record(") != 1:
        raise AssertionError("static miss evidence has an unverified recording path")

    handler = function_body(server, "handle_static_text_misses")
    parser = function_body(server, "static_text_miss_parse_filter")
    comparator = function_body(server, "static_text_miss_row_compare")
    require(
        r"g_static_text_miss_table.*?entry->misses",
        handler,
        "TCP command does not filter the dedicated evidence stream",
    )
    require(
        r"qsort\s*\(",
        handler,
        "static miss evidence is not deterministically ordered",
    )
    require(
        r"a->selected_hits\s*<\s*b->selected_hits.*?"
        r"a->selected_hits\s*>\s*b->selected_hits",
        comparator,
        "hot-first ordering does not use the selected evidence class",
    )
    require(
        r"json_get_str\s*\(json,\s*\"class\".*?"
        r"static_text_miss_parse_filter\s*\(filter_buf,\s*&filter\)",
        handler,
        "TCP command does not accept an explicit evidence class",
    )
    require(
        r"static_text_miss_selected_hits\s*\(entry,\s*filter\).*?"
        r"selected_hits\s*<\s*\(uint64_t\)min_hits",
        handler,
        "min_hits is not applied to the selected evidence class",
    )
    require(
        r'\\"modified_observations\\".*?\\"runtime_observations\\".*?'
        r'\\"unknown_observations\\"',
        handler,
        "TCP response can still hide non-static no-entry observations",
    )
    require(
        r'\\"misses\\".*?\\"modified\\".*?\\"runtime\\".*?\\"unknown\\"',
        handler,
        "TCP rows do not preserve per-PC classification evidence",
    )
    require(
        r'\\"class\\":\\"%s\\".*?\\"selected_hits\\"',
        handler,
        "TCP response does not identify or rank the selected evidence class",
    )
    for filter_name, enum_name in (
        ("pristine", "PRISTINE"),
        ("modified", "MODIFIED"),
        ("runtime", "RUNTIME"),
        ("unknown", "UNKNOWN"),
        ("all", "ALL"),
    ):
        require(
            rf'strcmp\s*\(name,\s*"{filter_name}"\)\s*==\s*0.*?'
            rf'STATIC_TEXT_MISS_FILTER_{enum_name}',
            parser,
            f"missing static-text evidence filter: {filter_name}",
        )
    if re.search(r"\b(?:fopen|fprintf|fwrite)\s*\(", handler):
        raise AssertionError("static miss telemetry writes a forbidden log file")
    require(
        r'\{\s*"static_text_misses"\s*,\s*handle_static_text_misses\s*\}',
        server,
        "static_text_misses command is not registered",
    )

    print("PASS: static-text misses are external-only and filterable by evidence class")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
