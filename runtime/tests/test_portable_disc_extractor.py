#!/usr/bin/env python3
"""Exercise the native launcher extractor against synthetic discs, never user media.

Usage: python test_portable_disc_extractor.py --cxx C:/msys64/ucrt64/bin/g++.exe
"""
import argparse
import pathlib
import struct
import subprocess
import tempfile


def record(name, lba, size, directory=False):
    name = name.encode('ascii')
    n = 33 + len(name) + (len(name) % 2 == 0)
    r = bytearray(n)
    r[0] = n
    struct.pack_into('<I', r, 2, lba)
    struct.pack_into('>I', r, 6, lba)
    struct.pack_into('<I', r, 10, size)
    struct.pack_into('>I', r, 14, size)
    r[25] = 2 if directory else 0
    r[28:32] = b'\x01\x00\x00\x01'
    r[32] = len(name)
    r[33:33 + len(name)] = name
    return r


def disc(exe_name='SLUS_005.84;1'):
    payload = b'PS-X synthetic test payload\x00' * 100
    data = bytearray(24 * 2048)
    pvd = memoryview(data)[16 * 2048:17 * 2048]
    pvd[0:7] = b'\x01CD001\x01'
    pvd[40:72] = b'SYNTHETIC'.ljust(32, b' ')
    root = record('\x00', 20, 2048, True)
    pvd[156:156 + len(root)] = root
    entry = record(exe_name, 21, len(payload))
    data[20 * 2048:20 * 2048 + len(entry)] = entry
    data[21 * 2048:21 * 2048 + len(payload)] = payload
    return bytes(data), payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    args = parser.parse_args()
    runtime = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='disc test ') as tmp:
        root = pathlib.Path(tmp)
        iso, expected = disc()
        (root / 'image with spaces.iso').write_bytes(iso)
        (root / 'expected.bin').write_bytes(expected)
        raw = bytearray(len(iso) // 2048 * 2352)
        for i in range(len(iso) // 2048):
            raw[i * 2352 + 24:i * 2352 + 24 + 2048] = iso[i * 2048:(i + 1) * 2048]
        (root / 'track one.bin').write_bytes(raw)
        (root / 'track two.bin').write_bytes(b'fake audio')
        (root / 'game with spaces.cue').write_text('FILE "track one.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\nFILE "track two.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n', encoding='ascii')
        (root / 'missing track.cue').write_text('FILE "absent.bin" BINARY\n  TRACK 01 MODE2/2352\n', encoding='ascii')
        (root / 'invalid.iso').write_bytes(b'not an image')
        no_exe, _ = disc('README.TXT;1')
        (root / 'no executable.iso').write_bytes(no_exe)
        truncated = bytearray(iso)
        # The directory claims more than the available payload sectors.
        struct.pack_into('<I', truncated, 20 * 2048 + 10, 9000)
        struct.pack_into('>I', truncated, 20 * 2048 + 14, 9000)
        (root / 'truncated payload.iso').write_bytes(truncated)
        harness = root / 'extract test.cpp'
        harness.write_text(r'''#include "disc_extractor.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
namespace fs = std::filesystem;
static std::vector<char> read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
  if (argc != 2) return 90;
  fs::path d(argv[1]); auto expected = read(d / "expected.bin");
  psx_launcher::DiscExtractor x;
  for (auto name : {"image with spaces.iso", "game with spaces.cue"}) {
    if (!x.open(d / name)) { std::cerr << "open: " << name; return 1; }
    fs::path out = d / (std::string(name) + ".out");
    if (!x.extract_primary_exe(out) || read(out) != expected) {
      std::cerr << "extract: " << name; return 2;
    }
    x.close();
  }
  for (auto name : {"missing track.cue", "invalid.iso"}) {
    if (x.open(d / name)) { std::cerr << "invalid accepted: " << name; return 3; }
  }
  if (!x.open(d / "no executable.iso")) return 4;
  if (x.extract_primary_exe(d / "should-not-exist")) return 5;
  if (fs::exists(d / "should-not-exist")) return 6;
  if (!x.open(d / "truncated payload.iso")) return 7;
  if (x.extract_primary_exe(d / "truncated.tmp")) return 8;
  if (!x.open(d / "image with spaces.iso")) return 9;
  // A directory is not a writable regular-file destination on any platform.
  if (x.extract_primary_exe(d)) return 10;
#ifndef _WIN32
  // /dev/full accepts open(), then fails on write/flush with ENOSPC.
  if (fs::exists("/dev/full") && x.extract_primary_exe("/dev/full")) return 11;
#endif
  std::cout << "PASS: synthetic ISO, multi-file CUE, spaces and invalid inputs\n";
}''', encoding='utf-8')
        output = root / 'extract test.exe'
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-static', '-I', str(runtime / 'launcher'), str(harness), str(runtime / 'launcher' / 'disc_extractor.cpp'), '-o', str(output)], check=True)
        subprocess.run([str(output), str(root)], check=True)


if __name__ == '__main__':
    main()
