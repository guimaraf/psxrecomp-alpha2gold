#!/usr/bin/env python3
"""Exercise generated overlay DIV/BREAK, including exceptional operands.

Usage: python test_overlay_div_break_codegen.py --recompiler PATH --cc PATH
The compiler builds only a tiny temporary C harness; no game is launched.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--recompiler', required=True)
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        header = bytearray(0x800)
        header[:8] = b'PS-X EXE'
        struct.pack_into('<I', header, 0x10, 0x80010000)
        struct.pack_into('<I', header, 0x18, 0x80010000)
        # div $v0,$a1; break 7; jr $ra; nop
        body = struct.pack('<4I', 0x0045001A, 0x0007000D, 0x03E00008, 0)
        struct.pack_into('<I', header, 0x1C, len(body))
        (tmp / 'test.psx').write_bytes(header + body)
        (tmp / 'seeds.txt').write_text('0x80010000\n', encoding='ascii')
        out = tmp / 'out'
        out.mkdir()
        env = dict(os.environ, PSX_CPS='1')
        run([args.recompiler, str(tmp / 'test.psx'), '--seeds', str(tmp / 'seeds.txt'),
             '--out-dir', str(out), '--overlay'], env=env)
        sources = list(out.glob('*_full.c'))
        if len(sources) != 1:
            raise AssertionError(f'expected one generated C file, got {sources}')
        source = sources[0].read_text(encoding='utf-8')
        if 'psx_break(cpu, 0x01C00u, 0x80010004u); return;' not in source:
            raise AssertionError('BREAK did not emit the expected trap code and PC')
        div = next((line.strip() for line in source.splitlines()
                    if 'int32_t psx_n' in line and 'psx_d' in line), None)
        if div is None:
            raise AssertionError('guarded signed DIV not found')
        harness = r'''#include <stdint.h>
#include <stdio.h>
typedef struct { uint32_t gpr[32], lo, hi; } CPUState;
static int check(uint32_t n, uint32_t d, uint32_t lo, uint32_t hi) {
  CPUState state = {0}; CPUState *cpu = &state;
  cpu->gpr[2] = n; cpu->gpr[5] = d;
''' + div + r'''
  if (cpu->lo != lo || cpu->hi != hi) { fprintf(stderr, "%08x/%08x -> %08x,%08x\n", n,d,cpu->lo,cpu->hi); return 1; }
  return 0;
}
int main(void) {
  return check(0x80000000u, 0xffffffffu, 0x80000000u, 0u)
      | check(0x80000000u, 0u, 1u, 0x80000000u)
      | check(5u, 0u, 0xffffffffu, 5u)
      | check((uint32_t)-7, 3u, (uint32_t)-2, (uint32_t)-1)
      | check(7u, (uint32_t)-3, (uint32_t)-2, 1u);
}
'''
        cfile = tmp / 'div.c'
        exe = tmp / ('div.exe' if os.name == 'nt' else 'div')
        cfile.write_text(harness, encoding='ascii')
        run([args.cc, str(cfile), '-o', str(exe)])
        run([str(exe)])
        print('PASS: generated overlay DIV normal/zero/overflow and BREAK trap emission')


if __name__ == '__main__':
    main()
