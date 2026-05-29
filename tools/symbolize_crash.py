import re
import subprocess
import sys
import os
import tempfile

def get_gdb_symbols(items_to_symbolize, gdb_path, elf_file):
    """
    Uses GDB in batch mode to resolve symbols and line numbers efficiently.
    """
    print(f"Resolving {len(items_to_symbolize)} unique addresses via GDB...")

    # Create a temporary GDB script to process all addresses in one launch
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.gdb') as f:
        script_path = f.name
        for item in items_to_symbolize:
            offset_hex = hex(item['offset'])
            # Print a unique delimiter with the address so we can parse the output easily
            f.write(f'printf "|||%s|||\\n", "{item["full_address"]}"\n')
            f.write(f'info symbol {offset_hex}\n')
            f.write(f'info line *{offset_hex}\n')

    try:
        # Run GDB in batch mode
        cmd = [gdb_path, '--batch', '-nx', '-ex', f'file {elf_file}', '-x', script_path]
        result = subprocess.run(cmd, capture_output=True, text=True)
        output = result.stdout
    finally:
        os.remove(script_path)

    symbol_map = {}

    # Split output by our custom markers: |||ADDRESS|||
    blocks = re.split(r'\|\|\|([0-9a-fA-F]+)\|\|\|', output)

    # Parse the blocks (Block 0 is garbage, 1 is addr, 2 is output, 3 is addr, etc.)
    for i in range(1, len(blocks) - 1, 2):
        address = blocks[i]
        block_text = blocks[i+1].strip()

        # --- Extract Function Name ---
        func_name = "??"
        # Look for "function_name + offset in section .text"
        symbol_match = re.search(r'^(.*?)(?:\s+in section|\s*$)', block_text, re.MULTILINE)
        if symbol_match and not symbol_match.group(1).startswith('No symbol'):
            func_name = symbol_match.group(1).strip()

        # --- Extract File and Line Info ---
        line_info = ""
        # Look for "Line X of "path/file.cpp" starts at..."
        line_match = re.search(r'Line (\d+) of "(.*?)"', block_text)
        if line_match:
            line_num = line_match.group(1)
            file_path = line_match.group(2)
            file_name = os.path.basename(file_path) # Get just the filename, not full path
            line_info = f" at {file_name}:{line_num}"

        # Combine them
        symbol_map[address] = f"{func_name}{line_info}"

    return symbol_map

def parse_crash_log(log_file, elf_file, gdb_path, parse_all_threads=False):
    with open(log_file, 'r') as f:
        content = f.read()

    # Look for module base address (just for validation/info)
    base_address = None
    module_section = re.search(r"Module 00:.*?Address:\s+([0-9a-fA-F]+)-", content, re.DOTALL)
    if module_section:
        base_address = int(module_section.group(1), 16)
    else:
        # Fallback for older formats
        x21_line = re.search(r"X\[21\]:\s+([0-9a-fA-F]+)\s+\(soh \+ 0x0\)", content)
        if x21_line:
            base_address = int(x21_line.group(1), 16)

    if base_address is None:
        print("Could not find module base address in the log file.")
        sys.exit(1)

    items_to_symbolize = []
    seen_addresses = set()
    in_thread_report = False

    # --- Collect Addresses ---
    for line in content.splitlines():
        if "Thread Report:" in line:
            in_thread_report = True

        # Stop collecting if we hit the Thread Report and the user didn't ask for all threads
        if in_thread_report and not parse_all_threads:
            continue

        # Find any address pattern: "0000000c667dd254 (module_name + 0x1dc2254)"
        match = re.search(r"([0-9a-fA-F]+)\s+\([a-zA-Z0-9_-]+\s*\+\s*0x([0-9a-fA-F]+)\)", line)
        if match:
            full_address = match.group(1)
            offset = int(match.group(2), 16)

            # Deduplicate so we don't query GDB for the same address twice
            if full_address not in seen_addresses:
                seen_addresses.add(full_address)
                items_to_symbolize.append({
                    'full_address': full_address,
                    'offset': offset
                })

    if not items_to_symbolize:
        print("Could not find any valid addresses to symbolize in the log file.")
        sys.exit(1)

    print(f"Found module base address: {hex(base_address)}")
    print(f"Using ELF file: {elf_file}")

    # Get all symbols
    symbol_map = get_gdb_symbols(items_to_symbolize, gdb_path, elf_file)

    # --- Inject symbols back into the log ---
    output_lines = []
    for line in content.splitlines():
        stripped_line = line.strip()

        # Does this line contain an address we parsed?
        match = re.search(r"([0-9a-fA-F]+)\s+\([a-zA-Z0-9_-]+\s*\+\s*0x[0-9a-fA-F]+\)", stripped_line)
        if match:
            full_address = match.group(1)
            if full_address in symbol_map:
                symbol = symbol_map[full_address]
                indentation = line[:len(line) - len(stripped_line)]

                # Strip old comments so they don't duplicate on re-runs
                base_line = stripped_line.split(" // ")[0]
                new_line = f"{indentation}{base_line} // {symbol}"
                output_lines.append(new_line)
                continue

        # If no address/symbol found, just keep the line as is
        output_lines.append(line)

    output_log_file = log_file.replace('.log', '_symbolized.log')
    with open(output_log_file, 'w') as f:
        f.write("\n".join(output_lines) + "\n")

    print(f"Successfully wrote symbolized log to: {output_log_file}")

if __name__ == '__main__':
    # Check for the flag and remove it from args so positional args still work
    parse_all_threads = False
    if '--all-threads' in sys.argv:
        parse_all_threads = True
        sys.argv.remove('--all-threads')

    if len(sys.argv) < 3:
        print("Usage: python3 symbolize_crash.py <crash_log.log> <executable.elf> [gdb_path] [--all-threads]")
        sys.exit(1)

    log_file = sys.argv[1]
    elf_file = sys.argv[2]

    # Default to devkitPro GDB if not provided
    gdb_path = sys.argv[3] if len(sys.argv) > 3 else "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb"

    parse_crash_log(log_file, elf_file, gdb_path, parse_all_threads)
