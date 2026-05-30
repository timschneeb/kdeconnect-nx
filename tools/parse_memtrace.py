#!/usr/bin/env python3
"""
parse_memtrace.py - Parse and symbolize kdec_memtrace.bin from MiniKDEConnect.

Binary format (little-endian):
  Header  : magic "KDMT" (4) + version u8 + pad(3) + anchor u64(8) = 16 bytes
            anchor = runtime address of MemTracker::init (used to auto-detect ASLR slide)
  Record  : type u8 | nframes u8 | size u32 | ptr u64 | [ts u64 (v2+)] | frames u64[nframes]
  type    : 'A' = alloc, 'F' = free
  ts      : ARM CNTPCT_EL0 counter value at alloc/free time (19.2 MHz on Switch)

Usage:
  python3 parse_memtrace.py <trace.bin> [sysmodule.elf] [options]

Options:
  --base ADDR    Override the runtime base address in hex.  Auto-detected from the
                 trace anchor + ELF symbol table when omitted.
  --gdb PATH     Path to aarch64-none-elf-gdb (default: devkitpro location).
  --top N        Entries to show per section (default: 20).
  --min-size N   Only show live allocs / hot spots >= N bytes (default: 0).
  --no-symbolize Skip GDB and print raw addresses.
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict

# ---------------------------------------------------------------------------
# Binary format
# ---------------------------------------------------------------------------

MAGIC    = b'KDMT'
# '<4sBxxxQ': 4s=magic, B=version, xxx=3 pad bytes (skipped), Q=anchor  →  16 bytes
_HDR_FMT  = struct.Struct('<4sBxxxQ')
_HDR_SIZE = _HDR_FMT.size  # 16

# v1: type(u8) nframes(u8) size(u32) ptr(u64)              = 14 bytes
# v2: type(u8) nframes(u8) size(u32) ptr(u64) ts(u64)      = 22 bytes
_REC_V1      = struct.Struct('<BBIQ')
_REC_V1_SIZE = _REC_V1.size   # 14
_REC_V2      = struct.Struct('<BBIQQ')
_REC_V2_SIZE = _REC_V2.size   # 22

# ARM CNTPCT_EL0 runs at 19.2 MHz on Nintendo Switch.
TICK_HZ = 19_200_000

def _ticks_to_ms(ticks: int) -> float:
    return ticks * 1000 / TICK_HZ


def parse_trace(path: str) -> tuple:
    """Return (records, anchor_addr, version).  Each record dict has 'ts' key (0 for v1)."""
    records = []
    anchor_addr = 0
    version = 1
    with open(path, 'rb') as f:
        raw_hdr = f.read(_HDR_SIZE)
        if len(raw_hdr) < 8 or raw_hdr[:4] != MAGIC:
            raise ValueError(f"Not a KDMT trace file (got {raw_hdr[:4]!r})")
        version = raw_hdr[4]
        if version not in (1, 2):
            raise ValueError(f"Unsupported version {version} (supported: 1, 2)")
        if len(raw_hdr) == _HDR_SIZE:
            _, _, anchor_addr = _HDR_FMT.unpack(raw_hdr)

        rec_fmt  = _REC_V2 if version == 2 else _REC_V1
        rec_size = rec_fmt.size

        while True:
            chunk = f.read(rec_size)
            if not chunk:
                break
            if len(chunk) < rec_size:
                print(f"Warning: truncated record at end of file, discarding {len(chunk)} bytes.",
                      file=sys.stderr)
                break
            if version == 2:
                type_byte, nframes, size, ptr, ts = rec_fmt.unpack(chunk)
            else:
                type_byte, nframes, size, ptr = rec_fmt.unpack(chunk)
                ts = 0
            kind = chr(type_byte)
            if kind not in ('A', 'F'):
                print(f"Warning: unknown record type {type_byte:#x}, aborting parse.",
                      file=sys.stderr)
                break
            frames: tuple = ()
            if nframes:
                raw = f.read(nframes * 8)
                if len(raw) < nframes * 8:
                    print("Warning: truncated frame data at end of file.", file=sys.stderr)
                    break
                frames = struct.unpack(f'<{nframes}Q', raw)
            records.append({'type': kind, 'size': size, 'ptr': ptr, 'ts': ts, 'frames': frames})
    return records, anchor_addr, version


_ANCHOR_MANGLED = '_ZN10MemTracker4initEPKc'  # MemTracker::init(char const*)

def _elf_vma_via_nm(elf_file: str, nm_path: str) -> int | None:
    """Return ELF VMA of MemTracker::init via nm, or None if not found."""
    try:
        out = subprocess.run(
            [nm_path, '--defined-only', elf_file],
            capture_output=True, text=True, timeout=30
        ).stdout
    except FileNotFoundError:
        return None
    for line in out.splitlines():
        if _ANCHOR_MANGLED in line or ('MemTracker' in line and 'init' in line):
            parts = line.split()
            if parts:
                try:
                    return int(parts[0], 16)
                except ValueError:
                    pass
    return None


def _elf_vma_via_gdb(elf_file: str, gdb_path: str) -> int | None:
    """Return ELF VMA of MemTracker::init via GDB info address, or None."""
    try:
        out = subprocess.run(
            [gdb_path, '--batch', '-nx',
             '-ex', f'file {elf_file}',
             '-ex', f'info address {_ANCHOR_MANGLED}'],
            capture_output=True, text=True, timeout=30
        ).stdout
    except FileNotFoundError:
        return None
    m = re.search(r'0x([0-9a-fA-F]+)', out)
    if m:
        return int(m.group(1), 16)
    return None


def detect_base(anchor_addr: int, elf_file: str, gdb_path: str) -> int:
    """Compute the ASLR slide: anchor_runtime - elf_vma(MemTracker::init)."""
    if not anchor_addr or not elf_file:
        return 0
    nm_path = re.sub(r'gdb$', 'nm', gdb_path)
    elf_vma = _elf_vma_via_nm(elf_file, nm_path) or _elf_vma_via_gdb(elf_file, gdb_path)
    if elf_vma is None:
        print("Warning: MemTracker::init not found in ELF (debug build with --wrap enabled?); base = 0.",
              file=sys.stderr)
        return 0
    slide = anchor_addr - elf_vma
    print(f"Auto-detected base: {slide:#x}  "
          f"(anchor {anchor_addr:#x} - ELF VMA {elf_vma:#x})", file=sys.stderr)
    return slide


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

def analyze(records: list) -> dict:
    live     = {}   # ptr -> {size, frames, serial, ts}
    hotspots = defaultdict(lambda: {'count': 0, 'total': 0, 'frames': ()})
    alloc_count = alloc_total = free_count = free_total = orphan_frees = serial = 0
    held_ms_samples: list[float] = []
    last_ts = 0

    for r in records:
        if r['ts']:
            last_ts = max(last_ts, r['ts'])
        if r['type'] == 'A':
            alloc_count += 1
            alloc_total += r['size']
            serial += 1
            live[r['ptr']] = {'size': r['size'], 'frames': r['frames'],
                               'serial': serial, 'ts': r['ts']}
            hs = hotspots[r['frames']]
            hs['count'] += 1
            hs['total'] += r['size']
            hs['frames'] = r['frames']
        else:
            free_count += 1
            free_total += r['size']
            if r['ptr'] in live:
                alloc_ts = live[r['ptr']]['ts']
                if alloc_ts and r['ts'] >= alloc_ts:
                    held_ms_samples.append(_ticks_to_ms(r['ts'] - alloc_ts))
                del live[r['ptr']]
            else:
                orphan_frees += 1

    held_ms_samples.sort()
    return {
        'alloc_count':      alloc_count,
        'alloc_total':      alloc_total,
        'free_count':       free_count,
        'free_total':       free_total,
        'orphan_frees':     orphan_frees,
        'live':             live,
        'hotspots':         sorted(hotspots.values(), key=lambda h: h['total'], reverse=True),
        'held_ms_samples':  held_ms_samples,
        'last_ts':          last_ts,
    }


# ---------------------------------------------------------------------------
# GDB symbolization (batched)
# ---------------------------------------------------------------------------

def symbolize(addrs: set, elf: str, gdb: str, base: int) -> dict:
    if not addrs:
        return {}
    unique = sorted(addrs)
    print(f"Symbolizing {len(unique)} unique addresses via GDB...", file=sys.stderr)

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.gdb') as f:
        script = f.name
        for addr in unique:
            offset = addr - base
            f.write(f'printf "|||{addr:016x}|||\\n"\n')
            f.write(f'info symbol {offset:#x}\n')
            f.write(f'info line *{offset:#x}\n')

    try:
        cmd = [gdb, '--batch', '-nx', '-ex', f'file {elf}', '-x', script]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180).stdout
    finally:
        os.remove(script)

    sym_map = {}
    blocks = re.split(r'\|\|\|([0-9a-fA-F]+)\|\|\|', out)
    for i in range(1, len(blocks) - 1, 2):
        addr    = int(blocks[i], 16)
        text    = blocks[i + 1].strip()
        fn      = '??'
        m = re.search(r'^(.*?)(?:\s+in section|\s*$)', text, re.MULTILINE)
        if m and not m.group(1).startswith('No symbol'):
            fn = m.group(1).strip()
        loc = ''
        m = re.search(r'Line (\d+) of "(.*?)"', text)
        if m:
            loc = f" at {os.path.basename(m.group(2))}:{m.group(1)}"
        sym_map[addr] = f"{fn}{loc}"

    return sym_map


# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------

def _fmt_bytes(n: int) -> str:
    if n >= 1 << 20: return f"{n / (1 << 20):.2f} MB"
    if n >= 1 << 10: return f"{n / (1 << 10):.1f} KB"
    return f"{n} B"


def _fmt_ms(ms: float) -> str:
    if ms >= 60_000: return f"{ms / 60_000:.1f} min"
    if ms >= 1_000:  return f"{ms / 1_000:.2f} s"
    return f"{ms:.1f} ms"


def _fmt_ts(ts: int, base_ts: int) -> str:
    """Format a tick as offset from base_ts in human-readable form."""
    if not ts:
        return "t=?"
    return f"t={_fmt_ms(_ticks_to_ms(ts - base_ts))}"


def _fmt_frames(frames: tuple, sym_map: dict, indent: str = '      ') -> str:
    if not frames:
        return f"{indent}(no frames)"
    lines = []
    for i, addr in enumerate(frames):
        sym = sym_map.get(addr, '??')
        lines.append(f"{indent}#{i:<2} {sym}  [{addr:#x}]")
    return '\n'.join(lines)


def _percentile(samples: list, pct: float) -> float:
    if not samples:
        return 0.0
    idx = int(len(samples) * pct / 100)
    return samples[min(idx, len(samples) - 1)]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Parse and symbolize kdec_memtrace.bin",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split('Usage:')[1] if 'Usage:' in __doc__ else '')
    ap.add_argument('trace',           help="Path to kdec_memtrace.bin")
    ap.add_argument('elf',  nargs='?', help="Path to MiniKDEConnect_sysmodule.elf (optional)")
    ap.add_argument('--base',     default=None,
                    help="Runtime base address of the sysmodule in hex. "
                         "Auto-detected from the trace anchor if omitted.")
    ap.add_argument('--gdb',
                    default='/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb',
                    help="Path to aarch64-none-elf-gdb")
    ap.add_argument('--top',      type=int, default=20,
                    help="Entries to show per section (default: 20)")
    ap.add_argument('--min-size', type=int, default=0,
                    help="Hide entries smaller than N bytes (default: 0)")
    ap.add_argument('--no-symbolize', action='store_true',
                    help="Skip GDB, print raw hex addresses")
    args = ap.parse_args()

    do_sym   = not args.no_symbolize and args.elf is not None
    min_size = args.min_size

    print(f"Parsing {args.trace} ...", file=sys.stderr)
    records, anchor_addr, version = parse_trace(args.trace)
    print(f"Parsed {len(records):,} records (format v{version}).", file=sys.stderr)

    if args.base is not None:
        base = int(args.base, 16)
    elif do_sym:
        base = detect_base(anchor_addr, args.elf, args.gdb)
    else:
        base = 0

    stats = analyze(records)

    # Tick offset for display: use first record's ts as t=0
    base_ts = records[0]['ts'] if records and records[0]['ts'] else 0

    all_addrs: set = set()
    for r in records:
        all_addrs.update(r['frames'])

    sym_map: dict = {}
    if do_sym:
        sym_map = symbolize(all_addrs, args.elf, args.gdb, base)

    # -----------------------------------------------------------------------
    # Report
    # -----------------------------------------------------------------------
    W = 70
    live_list    = sorted(stats['live'].items(), key=lambda kv: kv[1]['size'], reverse=True)
    live_bytes   = sum(v['size'] for v in stats['live'].values())
    hotspots     = [h for h in stats['hotspots'] if h['total'] >= min_size]
    live_showing = [kv for kv in live_list if kv[1]['size'] >= min_size]
    held         = stats['held_ms_samples']

    print()
    print('=' * W)
    print('  MiniKDEConnect Memory Trace')
    print('=' * W)
    print(f"  Trace   : {args.trace}  (v{version})")
    if args.elf:
        print(f"  ELF     : {args.elf}")
    if anchor_addr:
        print(f"  Anchor  : {anchor_addr:#x}  (MemTracker::init at runtime)")
    if base:
        print(f"  Base    : {base:#x}  ({'manual' if args.base else 'auto-detected'})")
    if base_ts:
        print(f"  t=0     : CNTPCT {base_ts:#x}  (first record)")
    print()

    print("--- Summary " + '-' * (W - 12))
    print(f"  Records      : {len(records):>10,}")
    print(f"  Allocs       : {stats['alloc_count']:>10,}  ({_fmt_bytes(stats['alloc_total'])})")
    print(f"  Frees        : {stats['free_count']:>10,}  ({_fmt_bytes(stats['free_total'])})")
    print(f"  Live at exit : {len(stats['live']):>10,}  ({_fmt_bytes(live_bytes)})")
    print(f"  Orphan frees : {stats['orphan_frees']:>10,}  "
          f"(free() of ptr not seen in alloc: pre-init or unwrapped allocator)")
    if held:
        print()
        print(f"  Hold-time stats ({len(held):,} freed allocs with timestamps):")
        print(f"    p50  = {_fmt_ms(_percentile(held, 50))}")
        print(f"    p90  = {_fmt_ms(_percentile(held, 90))}")
        print(f"    p99  = {_fmt_ms(_percentile(held, 99))}")
        print(f"    max  = {_fmt_ms(held[-1])}")
    print()

    # Live allocations
    has_ts = version >= 2
    show_n = min(args.top, len(live_showing))
    print(f"--- Live Allocations at Exit  "
          f"(top {show_n} of {len(live_showing)} >= {_fmt_bytes(min_size)} by size) "
          + '-' * max(0, W - 60))
    last_ts = stats['last_ts']
    for ptr, info in live_showing[:args.top]:
        age = ''
        if has_ts and info['ts'] and last_ts:
            held_ms = _ticks_to_ms(last_ts - info['ts'])
            age = f"  age={_fmt_ms(held_ms)}  alloc@{_fmt_ts(info['ts'], base_ts)}"
        print(f"  {ptr:#018x}  {_fmt_bytes(info['size'])}{age}")
        print(_fmt_frames(info['frames'], sym_map))
        print()

    # Hot spots
    show_n = min(args.top, len(hotspots))
    print(f"--- Allocation Hot Spots  "
          f"(top {show_n} of {len(hotspots)} >= {_fmt_bytes(min_size)} by total bytes) "
          + '-' * max(0, W - 56))
    for hs in hotspots[:args.top]:
        avg = hs['total'] // max(hs['count'], 1)
        print(f"  count={hs['count']:,}  total={_fmt_bytes(hs['total'])}  avg={_fmt_bytes(avg)}")
        print(_fmt_frames(hs['frames'], sym_map))
        print()


if __name__ == '__main__':
    main()
