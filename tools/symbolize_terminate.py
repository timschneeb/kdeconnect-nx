#!/usr/bin/env python3
"""
symbolize_terminate.py - Decode a terminate backtrace from kdeconnect_sysmodule.log.

The terminate handler logs:
  [E] terminate: anchor=0x<ADDR> symbol=log_backtrace
  [E] terminate: backtrace:
  [E]   #0   0x<ADDR>
  [E]   #1   0x<ADDR>
  ...

The anchor address is the runtime address of log_backtrace(). The script looks
up its ELF VMA via nm, computes the ASLR slide, then symbolizes each frame with GDB.

Usage:
  python3 symbolize_terminate.py <log_file> <sysmodule.elf> [--gdb PATH]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

DEFAULT_GDB = '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb'

# static void log_backtrace() mangles to _ZL13log_backtracev
_ANCHOR_MANGLED  = '_ZL13log_backtracev'
_ANCHOR_DEMANGLED = 'log_backtrace'


def find_anchor_vma(elf: str, gdb: str) -> int | None:
    nm = re.sub(r'gdb$', 'nm', gdb)
    try:
        out = subprocess.run(
            [nm, '--defined-only', elf],
            capture_output=True, text=True, timeout=30
        ).stdout
        for line in out.splitlines():
            if _ANCHOR_MANGLED in line or _ANCHOR_DEMANGLED in line:
                parts = line.split()
                if parts:
                    try:
                        return int(parts[0], 16)
                    except ValueError:
                        pass
    except FileNotFoundError:
        pass

    # Fallback: ask GDB
    try:
        out = subprocess.run(
            [gdb, '--batch', '-nx',
             '-ex', f'file {elf}',
             '-ex', f'info address {_ANCHOR_MANGLED}'],
            capture_output=True, text=True, timeout=30
        ).stdout
        m = re.search(r'0x([0-9a-fA-F]+)', out)
        if m:
            return int(m.group(1), 16)
    except FileNotFoundError:
        pass

    return None


def symbolize(offsets: list[int], elf: str, gdb: str) -> dict[int, str]:
    if not offsets:
        return {}
    print(f"Symbolizing {len(offsets)} frame(s) via GDB...", file=sys.stderr)

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.gdb') as f:
        script = f.name
        for off in offsets:
            f.write(f'printf "|||{off:016x}|||\\n"\n')
            f.write(f'info symbol {off:#x}\n')
            f.write(f'info line *{off:#x}\n')

    try:
        cmd = [gdb, '--batch', '-nx', '-ex', f'file {elf}', '-x', script]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout
    finally:
        os.remove(script)

    sym_map: dict[int, str] = {}
    blocks = re.split(r'\|\|\|([0-9a-fA-F]+)\|\|\|', out)
    for i in range(1, len(blocks) - 1, 2):
        off  = int(blocks[i], 16)
        text = blocks[i + 1].strip()
        fn   = '??'
        m = re.search(r'^(.*?)(?:\s+in section|\s*$)', text, re.MULTILINE)
        if m and not m.group(1).startswith('No symbol'):
            fn = m.group(1).strip()
        loc = ''
        m = re.search(r'Line (\d+) of "(.*?)"', text)
        if m:
            loc = f" at {os.path.basename(m.group(2))}:{m.group(1)}"
        sym_map[off] = f"{fn}{loc}"

    return sym_map


def parse_log(log_file: str) -> tuple[int | None, list[tuple[int, int, int]]]:
    """Return (anchor_runtime, [(frame_index, raw_addr, raw_addr), ...])."""
    anchor = None
    frames: list[tuple[int, int, int]] = []
    in_bt  = False

    with open(log_file) as f:
        for line in f:
            # anchor line
            m = re.search(r'terminate: anchor=0x([0-9a-fA-F]+) symbol=log_backtrace', line)
            if m:
                anchor = int(m.group(1), 16)
                in_bt  = False
                frames.clear()
                continue

            # backtrace header — starts collection
            if 'terminate: backtrace:' in line and anchor is not None:
                in_bt = True
                frames.clear()
                continue

            # frame lines
            if in_bt:
                m = re.search(r'#(\d+)\s+0x([0-9a-fA-F]+)', line)
                if m:
                    frames.append((int(m.group(1)), int(m.group(2), 16)))
                else:
                    in_bt = False  # end of backtrace block

    return anchor, frames


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log',  help="kdeconnect_sysmodule.log (or any log with a terminate block)")
    ap.add_argument('elf',  help="MiniKDEConnect_sysmodule.elf")
    ap.add_argument('--gdb', default=DEFAULT_GDB, help="Path to aarch64-none-elf-gdb")
    ap.add_argument('--no-symbolize', action='store_true', help="Print raw offsets only")
    args = ap.parse_args()

    anchor, frames = parse_log(args.log)

    if anchor is None:
        print("ERROR: no 'terminate: anchor=' line found in log.", file=sys.stderr)
        sys.exit(1)
    if not frames:
        print("ERROR: anchor found but no backtrace frames.", file=sys.stderr)
        sys.exit(1)

    print(f"Anchor  : {anchor:#x}  (runtime address of log_backtrace)")

    anchor_vma = find_anchor_vma(args.elf, args.gdb)
    if anchor_vma is None:
        print("ERROR: could not find log_backtrace in ELF — was the binary stripped?",
              file=sys.stderr)
        sys.exit(1)

    slide = anchor - anchor_vma
    print(f"ELF VMA : {anchor_vma:#x}")
    print(f"Slide   : {slide:#x}")
    print()

    offsets = [addr - slide for _, addr in frames]

    sym_map: dict[int, str] = {}
    if not args.no_symbolize:
        sym_map = symbolize(offsets, args.elf, args.gdb)

    print("Backtrace:")
    for (idx, raw), off in zip(frames, offsets):
        sym = sym_map.get(off, '??') if not args.no_symbolize else ''
        sym_str = f"  {sym}" if sym else ''
        print(f"  #{idx:<2}  {raw:#018x}  (offset {off:#x}){sym_str}")


if __name__ == '__main__':
    main()
