#!/usr/bin/env python3
"""Ensure a local Git checkout exists for external Tribe verification tests."""

from __future__ import annotations

import pathlib
import subprocess
import sys


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: ensure_git_repo.py <checkout-dir> <repo-url> <sentinel-path>", file=sys.stderr)
        return 2

    checkout = pathlib.Path(argv[1]).resolve()
    repo_url = argv[2]
    sentinel = checkout / argv[3]

    if sentinel.exists():
        return 0

    if checkout.exists() and any(checkout.iterdir()):
        print(f"{checkout} exists but does not contain {argv[3]}", file=sys.stderr)
        return 1

    checkout.parent.mkdir(parents=True, exist_ok=True)
    print(f"cloning {repo_url} into {checkout}")
    subprocess.run(["git", "clone", "--depth", "1", repo_url, str(checkout)], check=True)

    if not sentinel.exists():
        print(f"clone finished but {checkout} does not contain {argv[3]}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
