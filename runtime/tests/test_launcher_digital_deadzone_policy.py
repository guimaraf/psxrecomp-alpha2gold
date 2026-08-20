#!/usr/bin/env python3
"""Structural regression for the launcher's Digital deadzone policy.

Usage: python runtime/tests/test_launcher_digital_deadzone_policy.py
Exit 0 = PASS.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "runtime" / "launcher" / "launcher.cpp"
LAUNCHER_HEADER = ROOT / "runtime" / "launcher" / "launcher.h"
MAIN = ROOT / "runtime" / "src" / "main.cpp"


def require(pattern: str, source: str, message: str) -> None:
    if not re.search(pattern, source, re.S):
        raise AssertionError(message)


def main() -> int:
    source = LAUNCHER.read_text(encoding="utf-8")
    header = LAUNCHER_HEADER.read_text(encoding="utf-8")
    main_source = MAIN.read_text(encoding="utf-8")

    require(
        r"inline\s+constexpr\s+int\s+kDigitalDeadzonePct\s*=\s*40\s*;",
        header,
        "Digital deadzone policy is not fixed at 40 percent",
    )
    require(
        r"inline\s+constexpr\s+int\s+kPadAxisMax\s*=\s*32767\s*;.*?"
        r"deadzone_raw_to_pct\s*\(int\s+raw\).*?"
        r"\(raw\s*\*\s*100\s*\+\s*kPadAxisMax\s*/\s*2\)\s*/\s*kPadAxisMax.*?"
        r"deadzone_pct_to_raw\s*\(int\s+pct\).*?"
        r"pct\s*\*\s*kPadAxisMax\s*/\s*100",
        header,
        "deadzone raw/percent conversion does not preserve launcher steps",
    )
    require(
        r"seed\.deadzone\s*=\s*resolved_deadzone\s*>=\s*0\s*"
        r"\?\s*resolved_deadzone\s*:\s*"
        r"psx_launcher::deadzone_pct_to_raw\s*\("
        r"psx_launcher::kDigitalDeadzonePct\s*\)\s*;",
        main_source,
        "fresh launcher sessions are not seeded with the 40 percent default",
    )
    require(
        r"int\s+deadzone_pct\s*=\s*kDigitalDeadzonePct\s*;",
        source,
        "launcher model does not default to the Digital-safe deadzone",
    )
    require(
        r"m\.deadzone_pct\s*=\s*io\.has_deadzone\s*\?\s*"
        r"deadzone_raw_to_pct\s*\(io\.deadzone\)\s*:\s*"
        r"kDigitalDeadzonePct\s*;",
        source,
        "persisted deadzone is not preserved or the missing-value fallback is unsafe",
    )
    require(
        r"bool\s+select_player_pad_mode\s*\(.*?"
        r"mode\s*!=\s*PSXRecompV4::PAD_MODE_DIGITAL\s*\)\s*return\s+false\s*;.*?"
        r"m\.deadzone_pct\s*=\s*kDigitalDeadzonePct\s*;.*?return\s+true\s*;",
        source,
        "selecting Digital does not reset the deadzone without affecting other modes",
    )
    for player in (1, 2):
        require(
            rf'BindEventCallback\("set_mode_p{player}".*?'
            rf"select_player_pad_mode\s*\(m,\s*{player - 1},\s*mode\).*?"
            r'DirtyVariable\("deadzone_pct"\)',
            source,
            f"Player {player} Digital selection does not publish the deadzone reset",
        )
    require(
        r"io\.deadzone\s*=\s*deadzone_pct_to_raw\s*\(m\.deadzone_pct\)\s*;"
        r"\s*io\.has_deadzone\s*=\s*true\s*;",
        source,
        "a manual deadzone choice is no longer persisted when launching",
    )

    for pct in range(0, 51, 5):
        raw = pct * 32767 // 100
        restored_pct = (raw * 100 + 32767 // 2) // 32767
        if restored_pct != pct:
            raise AssertionError(
                f"deadzone step {pct}% round-trips through raw storage as {restored_pct}%"
            )

    print("PASS: Digital selects 40%; manual deadzone steps survive save/reload until reselection")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError) as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)
