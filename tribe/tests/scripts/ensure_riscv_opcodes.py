#!/usr/bin/env python3
"""Ensure a local riscv-opcodes checkout exists for tribe spec tests."""

from __future__ import annotations

import pathlib
import subprocess
import sys


REPO_URL = "https://github.com/riscv/riscv-opcodes.git"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: ensure_riscv_opcodes.py <checkout-dir>", file=sys.stderr)
        return 2

    checkout = pathlib.Path(argv[1]).resolve()
    extensions = checkout / "extensions"
    git_dir = checkout / ".git"

    if extensions.is_dir():
        return 0

    if checkout.exists() and any(checkout.iterdir()):
        print(f"{checkout} exists but is not a riscv-opcodes checkout", file=sys.stderr)
        return 1

    checkout.parent.mkdir(parents=True, exist_ok=True)
    print(f"cloning {REPO_URL} into {checkout}")
    subprocess.run(
        ["git", "clone", "--depth", "1", REPO_URL, str(checkout)],
        check=True,
    )

    if not git_dir.is_dir() or not extensions.is_dir():
        print(f"clone finished but {checkout} does not look like riscv-opcodes", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
