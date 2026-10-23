#!/usr/bin/env python3
"""Select an immutable Tribe test dependency, never an unversioned branch tip."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile

from riscv_dependencies import load_lock, repository_for_url


def git(checkout: pathlib.Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(checkout), *args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return result.stdout.strip()


def init_submodules(checkout: pathlib.Path) -> None:
    if (checkout / ".gitmodules").exists():
        status = git(checkout, "submodule", "status", "--recursive")
        if any(line.startswith(("-", "+", "U")) for line in status.splitlines()):
            git(checkout, "submodule", "update", "--init", "--recursive")


def ensure_checkout(checkout: pathlib.Path, url: str, sentinel: str,
                    revision: str, update: bool = False) -> None:
    # Different CTest widths share these source trees. Serialize first clone,
    # checkout and submodule setup instead of racing Git's index/config locks.
    lock_dir = pathlib.Path(tempfile.gettempdir()) / f"cpphdl-riscv-locks-{os.getuid()}"
    lock_dir.mkdir(mode=0o700, exist_ok=True)
    key = hashlib.sha256(str(checkout.resolve()).encode()).hexdigest()
    with (lock_dir / key).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        _ensure_checkout(checkout, url, sentinel, revision, update)


def _ensure_checkout(checkout: pathlib.Path, url: str, sentinel: str,
                     revision: str, update: bool) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Dependency revision must be a full immutable Git SHA")
    checkout = checkout.resolve()
    if checkout.exists() and not checkout.is_dir():
        raise ValueError(f"{checkout} is not a directory")
    if checkout.exists() and any(checkout.iterdir()):
        # A .git file (worktree) is valid, but inheriting the enclosing
        # cpphdl repository for a non-Git directory is not.
        if not (checkout / ".git").exists() or pathlib.Path(git(checkout, "rev-parse", "--show-toplevel")).resolve() != checkout:
            raise ValueError(f"{checkout} is not an independent Git checkout")
        current = git(checkout, "rev-parse", "HEAD")
        if current != revision:
            hint = "Run the dependency loader to select the locked revision on a clean checkout."
            if not update:
                raise ValueError(f"Dependency revision mismatch: {checkout}: expected {revision}, found {current}. {hint}")
            if git(checkout, "status", "--porcelain", "--untracked-files=normal"):
                raise ValueError(f"Refusing to change dirty dependency {checkout} from {current} to {revision}; preserve your edits first")
        else:
            if not (checkout / sentinel).exists():
                raise ValueError(f"Pinned checkout {checkout} is missing {sentinel}")
            # riscv-dv deliberately carries runner-generated compatibility
            # patches on this base revision. Never reset those local edits.
            init_submodules(checkout)
            return
    else:
        checkout.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "init", str(checkout)], check=True, capture_output=True)
        git(checkout, "remote", "add", "origin", url)

    present = subprocess.run(["git", "-C", str(checkout), "cat-file", "-e", revision + "^{commit}"],
                             capture_output=True).returncode == 0
    if not present:
        # Fetch the exact commit even when the remote's default branch moves.
        git(checkout, "fetch", "--depth", "1", url, revision)
    git(checkout, "checkout", "--detach", revision)
    if not (checkout / sentinel).exists():
        raise ValueError(f"Pinned checkout {checkout} is missing {sentinel}")
    init_submodules(checkout)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=pathlib.Path)
    parser.add_argument("repo_url")
    parser.add_argument("sentinel")
    parser.add_argument("--dependency", choices=load_lock()["repositories"])
    parser.add_argument("--update", action="store_true", help="allow clean existing checkouts to move to the locked revision")
    args = parser.parse_args(argv[1:])
    try:
        name = args.dependency or repository_for_url(args.repo_url)
        revision = load_lock()["repositories"][name]["revision"]
        ensure_checkout(args.checkout, args.repo_url, args.sentinel, revision, args.update)
        print(f"{name}: {revision} ({args.checkout})", flush=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
