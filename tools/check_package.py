#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the built add-on folder before it is packaged or installed."""

import argparse
from pathlib import Path
import struct
import subprocess
import sys

ADAPTER = "fsr4.so"
RUNTIME = "amd_fidelityfx_upscaler_vk.so"
REQUIRED = {
    ADAPTER,
    RUNTIME,
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "licenses/adapter-MIT.txt",
    "licenses/hailstone-SDK-MIT.txt",
    "licenses/runtime-GPL-3.0.txt",
}
EXPORTS = {
    ADAPTER: {"hailstone_upscaler"},
    RUNTIME: {"ffxCreateContext", "ffxDestroyContext", "ffxQuery", "ffxDispatch"},
}


def elf_header(path):
    header = path.read_bytes()[:64]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise ValueError(f"not an ELF file: {path}")
    if header[4] != 2 or header[5] != 1:
        raise ValueError(f"not a little-endian 64-bit ELF file: {path}")
    kind, machine = struct.unpack_from("<HH", header, 16)
    if kind != 3 or machine != 62:
        raise ValueError(f"not an x86-64 shared object: {path}")


def exported_symbols(path):
    result = subprocess.run(
        ["readelf", "--wide", "--dyn-syms", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return {line.split()[-1] for line in result.stdout.splitlines() if line.strip()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    args = parser.parse_args()
    errors = []

    for name in sorted(REQUIRED):
        if not (args.dist / name).is_file():
            errors.append(f"missing {name}")

    for path in args.dist.rglob("*"):
        if path.is_file() and "framegeneration" in path.name.lower():
            errors.append(f"Frame Generation artifact must not be packaged: {path.name}")

    for name, wanted in EXPORTS.items():
        path = args.dist / name
        if not path.is_file():
            continue
        try:
            elf_header(path)
            symbols = exported_symbols(path)
            for symbol in sorted(wanted - symbols):
                errors.append(f"{name} does not export {symbol}")
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            errors.append(str(error))

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"package OK: {args.dist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
