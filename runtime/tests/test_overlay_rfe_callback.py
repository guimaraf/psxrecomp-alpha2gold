#!/usr/bin/env python3
"""Structural regression test for overlay-resident RFE forwarding.

Generated overlay code can contain a real guest ``rfe`` instruction and call
``psx_rfe_mark_escape()``. The DLL glue must flush pending cycles and forward
that call through the versioned overlay ABI to the runtime's real exception
state. A missing link in this chain makes every affected DLL fail to link.

Usage: python runtime/tests/test_overlay_rfe_callback.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
API = ROOT / "runtime" / "include" / "overlay_api.h"
LOADER = ROOT / "runtime" / "src" / "overlay_loader.c"
INTERRUPTS = ROOT / "runtime" / "src" / "interrupts.c"
GLUE = ROOT / "tools" / "compile_overlays.py"
EMITTERS = (
    ROOT / "recompiler" / "src" / "code_generator.cpp",
    ROOT / "recompiler" / "src" / "full_function_emitter.cpp",
)


def require(pattern: str, source: str, message: str) -> re.Match[str]:
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError(message)
    return match


def function_body(source: str, name: str) -> str:
    match = require(
        rf"\bvoid\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{",
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
    api = API.read_text(encoding="utf-8")
    loader = LOADER.read_text(encoding="utf-8")
    interrupts = INTERRUPTS.read_text(encoding="utf-8")
    glue = GLUE.read_text(encoding="utf-8")

    require(
        r"#define\s+PSX_OVERLAY_ABI_VERSION\s+12\b",
        api,
        "overlay ABI must be bumped to v12 for the appended RFE callback",
    )
    require(
        r"#define\s+PSX_OVERLAY_CODEGEN_VER\s+5\b",
        api,
        "overlay codegen version must be bumped to cg5 for fresh cache coverage",
    )
    require(
        r"gte_write_ctrl\s*\)\s*\([^;]*;\s*"
        r"/\*.*?ABI v12.*?\*/\s*"
        r"void\s*\(\*psx_rfe_mark_escape\)\s*\(void\)\s*;\s*"
        r"\}\s*OverlayCallbacks\s*;",
        api,
        "RFE callback must remain the final OverlayCallbacks member",
    )

    init = function_body(loader, "init_callbacks")
    require(
        r"s_callbacks\.psx_rfe_mark_escape\s*=\s*psx_rfe_mark_escape\s*;",
        init,
        "overlay loader does not publish the runtime RFE implementation",
    )

    runtime_rfe = function_body(interrupts, "psx_rfe_mark_escape")
    require(
        r"g_rfe_escape_pending\s*=\s*1\s*;",
        runtime_rfe,
        "runtime RFE callback no longer marks the exception escape pending",
    )
    require(
        r"g_exc_escape_reason\s*=\s*PSX_EXC_ESCAPE_RFE_RETURN\s*;",
        runtime_rfe,
        "runtime RFE callback no longer records the RFE escape reason",
    )

    glue_rfe = function_body(glue, "psx_rfe_mark_escape")
    flush = require(
        r"overlay_flush_cycles\s*\(\s*\)\s*;",
        glue_rfe,
        "overlay RFE wrapper does not flush pending cycles",
    )
    forward = require(
        r"g_cbs\.psx_rfe_mark_escape\s*\(\s*\)\s*;",
        glue_rfe,
        "overlay RFE wrapper does not call the runtime callback",
    )
    if flush.start() > forward.start():
        raise AssertionError("overlay RFE wrapper forwards before flushing cycles")

    for emitter in EMITTERS:
        source = emitter.read_text(encoding="utf-8")
        require(
            r"psx_rfe_mark_escape\s*\(\s*\)\s*;",
            source,
            f"RFE emitter call missing from {emitter.name}",
        )

    print("PASS: overlay RFE calls forward to the runtime through ABI v12/cg5")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
