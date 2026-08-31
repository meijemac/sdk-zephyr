#!/usr/bin/env python3
"""Generate Intel HEX with load addresses equal to VMA (for FLIP simulator)."""

import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <objdump> <elf> <out.hex>", file=sys.stderr)
        return 1

    objdump, elf, out_hex = sys.argv[1:4]
    output = subprocess.check_output([objdump, "-h", elf], text=True)

    args = [objdump.replace("objdump", "objcopy"), "-O", "ihex"]
    for line in output.splitlines():
        match = re.match(
            r"\s*\d+\s+(\S+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+",
            line,
        )
        if not match:
            continue

        name, vma, lma = match.group(1), int(match.group(2), 16), int(match.group(3), 16)
        if vma != lma:
            args.append(f"--change-section-lma={name}={vma:#x}")

    args.extend([elf, out_hex])
    subprocess.check_call(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
