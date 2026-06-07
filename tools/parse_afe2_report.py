#!/usr/bin/env python3
# Disclaimer: this script was largely AI generated for debugging purposes.
"""
Parse an Atmosphere fatal-error report binary (AFE2 format) and optionally
symbolize the stack trace against an ELF file.

Usage:
    python3 parse_afe2_report.py <report.bin> [<executable.elf> [<gdb_path>]]

The ELF and GDB arguments are optional.  Without them the report is printed
with raw addresses only.  When provided, every stack-trace entry that falls
inside the module is annotated with its function name and source location.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

# -- AFE2 binary layout (little-endian, matches atmosphere_fatal_error_ctx) ---
# panic.h: atmosphere_fatal_error_ctx (magic == 0x32454641 "AFE2")
#
# uint32_t  magic
# uint32_t  error_desc
# uint64_t  title_id
# uint64_t  gprs[32]          (includes fp=gprs[29], lr=gprs[30], sp=gprs[31])
# uint64_t  pc
# uint64_t  module_base
# uint32_t  pstate
# uint32_t  afsr0
# uint32_t  afsr1
# uint32_t  esr
# uint64_t  far
# uint64_t  report_identifier
# uint64_t  stack_trace_size
# uint64_t  stack_dump_size
# uint64_t  stack_trace[0x20]
# uint8_t   stack_dump[0x100]
# uint8_t   tls[0x100]

AMS_FATAL_ERROR_MAX_STACKTRACE = 0x20
AMS_FATAL_ERROR_MAX_STACKDUMP  = 0x100
AMS_FATAL_ERROR_TLS_SIZE       = 0x100
ATMOSPHERE_REBOOT_TO_FATAL_MAGIC = 0x32454641  # "AFE2"

AFE2_FMT = (
    "<"
    "I"          # magic
    "I"          # error_desc
    "Q"          # title_id
    "32Q"        # gprs[32]
    "Q"          # pc
    "Q"          # module_base
    "I"          # pstate
    "I"          # afsr0
    "I"          # afsr1
    "I"          # esr
    "Q"          # far
    "Q"          # report_identifier
    "Q"          # stack_trace_size
    "Q"          # stack_dump_size
    f"{AMS_FATAL_ERROR_MAX_STACKTRACE}Q"   # stack_trace
    f"{AMS_FATAL_ERROR_MAX_STACKDUMP}s"    # stack_dump
    f"{AMS_FATAL_ERROR_TLS_SIZE}s"         # tls
)

AFE2_SIZE = struct.calcsize(AFE2_FMT)

MODULE_RANGE = 0x1_000_000  # assume module image ≤ 16 MiB


def parse_afe2(data: bytes) -> dict:
    if len(data) < AFE2_SIZE:
        raise ValueError(f"File too small: {len(data)} bytes, expected {AFE2_SIZE}")

    fields = struct.unpack_from(AFE2_FMT, data)
    idx = 0
    def take(n=1):
        nonlocal idx
        v = fields[idx:idx+n]
        idx += n
        return v[0] if n == 1 else v

    magic           = take()
    error_desc      = take()
    title_id        = take()
    gprs            = list(take(32))
    pc              = take()
    module_base     = take()
    pstate          = take()
    afsr0           = take()
    afsr1           = take()
    esr             = take()
    far             = take()
    report_id       = take()
    stack_trace_sz  = take()
    stack_dump_sz   = take()
    stack_trace_raw = list(take(AMS_FATAL_ERROR_MAX_STACKTRACE))
    stack_dump      = take()
    tls             = take()

    if magic != ATMOSPHERE_REBOOT_TO_FATAL_MAGIC:
        raise ValueError(f"Unexpected magic: 0x{magic:08X} (expected AFE2 = 0x{ATMOSPHERE_REBOOT_TO_FATAL_MAGIC:08X})")

    stack_trace = stack_trace_raw[:stack_trace_sz]

    return dict(
        magic=magic, error_desc=error_desc, title_id=title_id,
        gprs=gprs, pc=pc, module_base=module_base,
        pstate=pstate, afsr0=afsr0, afsr1=afsr1, esr=esr,
        far=far, report_id=report_id,
        stack_trace=stack_trace, stack_trace_sz=stack_trace_sz,
        stack_dump=stack_dump, stack_dump_sz=stack_dump_sz,
        tls=tls,
    )


def addr_str(addr: int, module_base: int) -> str:
    """Return 'addr (MOD_BASE + offset)' when addr is inside the module."""
    if module_base and (module_base <= addr < module_base + MODULE_RANGE):
        return f"0x{addr:016x} (MOD_BASE + 0x{addr - module_base:x})"
    return f"0x{addr:016x}"


def print_report(ctx: dict, symbol_map: dict | None = None) -> str:
    lines = []

    def emit(s=""):
        lines.append(s)

    def sym(addr):
        key = f"{addr - ctx['module_base']:x}" if ctx['module_base'] else None
        s = addr_str(addr, ctx['module_base'])
        if symbol_map and key and key in symbol_map:
            s += f"  // {symbol_map[key]}"
        return s

    emit("Fatal report (AFE2):")
    emit(f"  Magic:             AFE2 (0x{ctx['magic']:08X})")
    emit(f"  Error description: 0x{ctx['error_desc']:08X}")
    emit(f"  Title ID:          {ctx['title_id']:016X}")
    emit()

    emit("Registers:")
    gprs = ctx['gprs']
    mb   = ctx['module_base']
    for i in range(29):
        emit(f"  X[{i:02d}]: {sym(gprs[i])}")
    emit(f"  FP:   {sym(gprs[29])}")
    emit(f"  LR:   {sym(gprs[30])}")
    emit(f"  SP:   {sym(gprs[31])}")
    emit(f"  PC:   {sym(ctx['pc'])}")
    emit()

    emit("Exception info:")
    emit(f"  pstate:            0x{ctx['pstate']:08x}")
    emit(f"  afsr0:             0x{ctx['afsr0']:08x}")
    emit(f"  afsr1:             0x{ctx['afsr1']:08x}")
    emit(f"  esr:               0x{ctx['esr']:08x}")
    emit(f"  far:               0x{ctx['far']:016x}")
    emit(f"  report_identifier: 0x{ctx['report_id']:016x}")
    emit()

    emit(f"Stack trace ({ctx['stack_trace_sz']} entries):")
    for i, addr in enumerate(ctx['stack_trace']):
        emit(f"  [{i:02d}] {sym(addr)}")
    emit()

    emit(f"Stack dump ({ctx['stack_dump_sz']} bytes, base SP = 0x{gprs[31]:016x}):")
    emit("        00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f")
    emit("  " + "-" * 58)
    dump = ctx['stack_dump']
    for i in range(0, ctx['stack_dump_sz'], 16):
        row = dump[i:i+16]
        hex_left  = " ".join(f"{b:02x}" for b in row[:8])
        hex_right = " ".join(f"{b:02x}" for b in row[8:16])
        emit(f"  {i:04x}  {hex_left}  {hex_right}")
    emit()

    emit(f"TLS dump ({AMS_FATAL_ERROR_TLS_SIZE} bytes):")
    emit("        00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f")
    emit("  " + "-" * 58)
    tls = ctx['tls']
    for i in range(0, AMS_FATAL_ERROR_TLS_SIZE, 16):
        row = tls[i:i+16]
        hex_left  = " ".join(f"{b:02x}" for b in row[:8])
        hex_right = " ".join(f"{b:02x}" for b in row[8:16])
        emit(f"  {i:04x}  {hex_left}  {hex_right}")

    return "\n".join(lines)


# -- GDB symbolization (same approach as symbolize_crash.py) ------------------

def get_gdb_symbols(offsets: list[int], elf_file: str, gdb_path: str) -> dict:
    """
    Resolve a list of module-relative offsets to 'function at file:line' strings.
    Returns a dict mapping hex-string offset → symbol string.
    """
    print(f"Resolving {len(offsets)} addresses via GDB...")
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.gdb') as f:
        script_path = f.name
        for off in offsets:
            f.write(f'printf "|||{off:x}|||\\n"\n')
            f.write(f'info symbol {hex(off)}\n')
            f.write(f'info line *{hex(off)}\n')

    try:
        cmd = [gdb_path, '--batch', '-nx', '-ex', f'file {elf_file}', '-x', script_path]
        result = subprocess.run(cmd, capture_output=True, text=True)
        output = result.stdout
    finally:
        os.remove(script_path)

    symbol_map = {}
    blocks = re.split(r'\|\|\|([0-9a-fA-F]+)\|\|\|', output)
    for i in range(1, len(blocks) - 1, 2):
        key       = blocks[i]
        block     = blocks[i+1].strip()

        func_name = "??"
        m = re.search(r'^(.*?)(?:\s+in section|\s*$)', block, re.MULTILINE)
        if m and not m.group(1).startswith('No symbol'):
            func_name = m.group(1).strip()

        line_info = ""
        m = re.search(r'Line (\d+) of "(.*?)"', block)
        if m:
            line_info = f" at {os.path.basename(m.group(2))}:{m.group(1)}"

        symbol_map[key] = f"{func_name}{line_info}"

    return symbol_map


def collect_offsets(ctx: dict) -> list[int]:
    """Return deduplicated module-relative offsets from PC, LR and stack trace."""
    mb   = ctx['module_base']
    seen = set()
    result = []

    def add(addr):
        if mb and mb <= addr < mb + MODULE_RANGE:
            off = addr - mb
            if off not in seen:
                seen.add(off)
                result.append(off)

    add(ctx['pc'])
    add(ctx['gprs'][30])  # LR
    for addr in ctx['stack_trace']:
        add(addr)

    return result


# -- Entry point ---------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 parse_afe2_report.py <report.bin> [<executable.elf> [<gdb_path>]]")
        sys.exit(1)

    bin_file = sys.argv[1]
    elf_file = sys.argv[2] if len(sys.argv) > 2 else None
    gdb_path = sys.argv[3] if len(sys.argv) > 3 else \
               "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb"

    with open(bin_file, 'rb') as f:
        data = f.read()

    ctx = parse_afe2(data)

    symbol_map = None
    if elf_file:
        offsets = collect_offsets(ctx)
        if offsets:
            symbol_map = get_gdb_symbols(offsets, elf_file, gdb_path)
        else:
            print("No module-relative addresses found; skipping symbolization.")

    report = print_report(ctx, symbol_map)
    print(report)

    out_path = bin_file.replace('.bin', '.log') if bin_file.endswith('.bin') else bin_file + '.log'
    with open(out_path, 'w') as f:
        f.write(report + "\n")
    print(f"\nReport written to: {out_path}")


if __name__ == '__main__':
    main()
