#!/usr/bin/env python3
"""Ensure a local riscv-opcodes checkout exists for tribe spec tests."""

from __future__ import annotations

import pathlib
import subprocess
import sys

from ensure_git_repo import ensure_checkout
from riscv_dependencies import load_lock

REPO_URL = "https://github.com/riscv/riscv-opcodes.git"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: ensure_riscv_opcodes.py <checkout-dir>", file=sys.stderr)
        return 2

    checkout = pathlib.Path(argv[1]).resolve()
    try:
        ensure_checkout(checkout, REPO_URL, "extensions",
                        load_lock()["repositories"]["riscv-opcodes"]["revision"])
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
