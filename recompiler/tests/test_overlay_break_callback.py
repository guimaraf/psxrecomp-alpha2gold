#!/usr/bin/env python3
"""Exercise the real generated overlay shim's BREAK callback and cycle flush.

Builds a disposable native harness, without loading a game or a shard.
Usage: python test_overlay_break_callback.py --cc C:/msys64/ucrt64/bin/gcc.exe
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('compile_overlays', repo / 'tools' / 'compile_overlays.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    harness = r'''
#include <stdio.h>
#include <string.h>
static unsigned event, calls, cycles_seen, code_seen, pc_seen;
static CPUState *cpu_seen;
static void on_cycles(uint32_t n) { if (event != 0) event = 99; else event = 1; cycles_seen += n; }
static void on_break(CPUState *cpu, uint32_t code, uint32_t pc) {
    if (event != 1) event = 99; else event = 2;
    ++calls; cpu_seen = cpu; code_seen = code; pc_seen = pc;
}
int main(void) {
    OverlayCallbacks cb; CPUState cpu;
    memset(&cb, 0, sizeof cb); memset(&cpu, 0, sizeof cpu);
    cb.advance_cycles = on_cycles; cb.psx_break = on_break;
    if (overlay_abi() != PSX_OVERLAY_ABI_TAG) return 1;
    overlay_init(&cb);
    psx_advance_cycles(11); psx_advance_cycles(26);
    psx_break(&cpu, 0x1c00u, 0x801e88bcu);
    overlay_flush_cycles();
    if (event != 2 || calls != 1 || cycles_seen != 37 || cpu_seen != &cpu ||
        code_seen != 0x1c00u || pc_seen != 0x801e88bcu) return 2;
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        source = tmp / 'callback_test.c'
        source.write_text(mod.DISPATCH_PREAMBLE + harness, encoding='utf-8')
        exe = tmp / 'callback_test.exe'
        result = subprocess.run([args.cc, '-std=c99', '-DPSX_OVERLAY_DLL_BUILD',
                                 '-DPSX_OVERLAY_FLAVOR=0', '-I', str(repo / 'runtime' / 'include'),
                                 str(source), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('compile failed:\n' + result.stdout + result.stderr)
        subprocess.run([str(exe)], check=True)
    print('PASS: actual overlay shim forwards BREAK code/PC/CPU after flushing cycles once')


if __name__ == '__main__':
    main()
