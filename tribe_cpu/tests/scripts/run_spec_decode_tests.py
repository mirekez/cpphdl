#!/usr/bin/env python3
"""CTest wrapper for tribe spec decode tests."""

from __future__ import annotations

import pathlib
import subprocess
import sys

import ensure_riscv_opcodes


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: run_spec_decode_tests.py <riscv-opcodes-dir> <test-binary>", file=sys.stderr)
        return 2

    opcodes_dir = pathlib.Path(argv[1])
    test_binary = pathlib.Path(argv[2])

    rc = ensure_riscv_opcodes.main(["ensure_riscv_opcodes.py", str(opcodes_dir)])
    if rc != 0:
        return rc

    return subprocess.run([str(test_binary)]).returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
