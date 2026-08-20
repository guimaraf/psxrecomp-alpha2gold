#!/usr/bin/env python3
"""Regression test for exact overlay capture micro-batch filtering.

Usage: python runtime/tests/test_compile_overlays_capture_filter.py
Exit 0 = PASS.
"""

from pathlib import Path
import argparse
import base64
import binascii
from contextlib import redirect_stderr
import importlib.util
import io
import sys
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "compile_overlays.py"
SPEC = importlib.util.spec_from_file_location("compile_overlays", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def make_capture(load_addr: int, data: bytes) -> dict:
    return {
        "load_addr": f"0x{load_addr:08X}",
        "size": len(data),
        "bytes_b64": base64.b64encode(data).decode("ascii"),
    }


def assert_cli_error(extra_args: list[str], expected: str) -> None:
    argv = [
        "compile_overlays.py",
        "--game-toml", "unused.toml",
        "--recompiler", "unused-recompiler",
        "--runtime-include", "unused-include",
        *extra_args,
    ]
    stderr = io.StringIO()
    with patch.object(sys, "argv", argv), redirect_stderr(stderr):
        try:
            MODULE.main()
        except SystemExit as exc:
            assert exc.code == 2, f"unexpected argparse exit code: {exc.code}"
        else:
            raise AssertionError("invalid CLI combination did not fail")
    assert expected in stderr.getvalue(), stderr.getvalue()


def main() -> int:
    stage10 = make_capture(0x801AE000, b"stage10-code")
    same_region_other_variant = make_capture(0x801AE000, b"other-code")
    main_text = make_capture(0x80010000, b"main-text")
    captures = [main_text, stage10, same_region_other_variant]

    stage10_crc = binascii.crc32(b"stage10-code") & 0xFFFFFFFF
    key = MODULE.parse_capture_key(f"0x801AE000:0x{stage10_crc:08X}")
    assert key == (0x001AE000, stage10_crc), "virtual region was not canonicalized"

    selected, missing = MODULE.select_captures_by_key(captures, [key])
    assert selected == [stage10], "filter selected the wrong same-address variant"
    assert not missing, "present capture key was reported missing"

    absent = (0x001AE000, 0xDEADBEEF)
    selected, missing = MODULE.select_captures_by_key(captures, [absent])
    assert selected == [], "absent key selected a capture"
    assert missing == {absent}, "absent key did not fail closed"

    selected, missing = MODULE.select_captures_by_key(captures, [])
    assert selected is captures and not missing, "default path must keep all captures"

    assert MODULE.parse_entry_pc("0x801EA2C0") == 0x001EA2C0
    assert MODULE.parse_entry_pc("0x001EA4E0") == 0x001EA4E0

    classified_seeds = [
        "0x801EA2C0",
        "interior 0x801EA2DC",
        "dispatch_root 0x80000CF0",
        "0x801EA4E0",
    ]
    requested = {0x001EA2C0, 0x001EA4E0}
    filtered, missing = MODULE.select_overlay_seeds(classified_seeds, requested)
    assert filtered == ["0x801EA2C0", "0x801EA4E0"]
    assert not missing, "approved entry selection unexpectedly failed"

    filtered, missing = MODULE.select_overlay_seeds(
        classified_seeds, {0x001EA2DC})
    assert filtered == ["interior 0x801EA2DC"], (
        "entry filtering promoted or discarded the interior marker")
    assert not missing

    filtered, missing = MODULE.select_overlay_seeds(
        classified_seeds, {0x001EA2C0, 0x001EA600})
    assert filtered == ["0x801EA2C0"]
    assert missing == {0x001EA600}, "unapproved entry did not fail closed"

    fragment_candidates = {
        0x801E0184, 0x801E03F0, 0x801EA2C0, 0x801EA4E0,
    }
    assert MODULE.select_overlay_addresses(
        fragment_candidates, requested) == {0x801EA2C0, 0x801EA4E0}, (
        "unselected orphan interiors leaked into the filtered fragment pass")

    pair_key = MODULE.entry_selection_cache_key(stage10_crc, requested)
    assert pair_key == MODULE.entry_selection_cache_key(
        stage10_crc, reversed(sorted(requested)))
    assert pair_key != MODULE.entry_selection_cache_key(
        stage10_crc ^ 1, requested), "bundle key ignored capture identity"
    assert pair_key != MODULE.entry_selection_cache_key(
        stage10_crc, {0x001EA2C0}), "bundle key ignored entry selection"
    assert pair_key != stage10_crc, (
        "diagnostic subset falsely reused the whole-capture CRC key")

    for invalid in ("0x001AE000", "bad:0x1", "0x1:0x100000000"):
        try:
            MODULE.parse_capture_key(invalid)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"invalid capture key accepted: {invalid}")

    for invalid in ("bad", "0x801EA2C2", "0x80200000", "0x100000000"):
        try:
            MODULE.parse_entry_pc(invalid)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"invalid entry PC accepted: {invalid}")

    entry_args = ["--entry", "0x801EA2C0"]
    exact_capture = ["--capture-key", "0x001E0000:0x06418369"]
    assert_cli_error(entry_args, "requires exactly one distinct --capture-key")
    assert_cli_error(exact_capture + entry_args + ["--static"],
                     "available only for DLL-cache mode")
    assert_cli_error(exact_capture + entry_args +
                     ["--force-interior", "0x801EA2DC"],
                     "cannot be combined with --force-interior")

    print("PASS: capture/entry filtering is canonical, marker-safe, and fail-closed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, KeyError, ValueError) as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
