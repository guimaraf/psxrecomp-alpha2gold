#!/usr/bin/env python3
"""Regression for linear, string-aware raw TCP response collection.

Usage: python runtime/tests/test_raw_tcp_large_response.py
Exit 0 = PASS.
"""

from pathlib import Path
import importlib.util
import json
import sys


ROOT = Path(__file__).resolve().parents[2]
RAW_TCP = ROOT / "tools" / "raw_tcp.py"
RAW_TCP_LONG = ROOT / "tools" / "raw_tcp_long.py"


class ChunkSocket:
    def __init__(self, chunks):
        self.chunks = iter(chunks)

    def recv(self, _size):
        return next(self.chunks, b"")


def load_raw_tcp():
    spec = importlib.util.spec_from_file_location("raw_tcp_under_test", RAW_TCP)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load tools/raw_tcp.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    source = RAW_TCP.read_text(encoding="utf-8")
    long_source = RAW_TCP_LONG.read_text(encoding="utf-8")
    if "data += bytes" in source or "data += bytes" in long_source:
        raise AssertionError("quadratic immutable-bytes accumulation returned")
    if "bytearray()" not in source:
        raise AssertionError("raw_tcp does not use a mutable linear buffer")
    if "from raw_tcp import run" not in long_source:
        raise AssertionError("raw_tcp_long does not share the corrected reader")

    module = load_raw_tcp()
    blob = ("x" * (2 * 1024 * 1024)) + '\\"escaped{[]}tail'
    expected = json.dumps({"id": 1, "ok": True, "blob": blob}).encode()
    wire = expected + b"\n{\"id\":2,\"ignored\":true}\n"
    cuts = (7, 65531, 131089, 524301, len(wire))
    chunks = []
    start = 0
    for end in cuts:
        chunks.append(wire[start:end])
        start = end

    received = module.recv_json_response(ChunkSocket(chunks))
    if received != expected:
        raise AssertionError(
            f"collector returned {len(received)} bytes, expected {len(expected)}"
        )
    decoded = json.loads(received.decode("utf-8"))
    if decoded["blob"] != blob:
        raise AssertionError("large escaped JSON string changed during collection")

    print(f"PASS: raw TCP reader collected {len(received)} bytes in one linear buffer")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
