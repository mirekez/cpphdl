"""Shared, checked-in dependency identities for Tribe verification."""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys


LOCK_FILE = pathlib.Path(__file__).resolve().parents[1] / "riscv-tools.lock.json"


def load_lock() -> dict:
    return json.loads(LOCK_FILE.read_text(encoding="utf-8"))


def repository_for_url(url: str) -> str:
    normalized = url.rstrip("/").removesuffix(".git")
    for name, entry in load_lock()["repositories"].items():
        if normalized == entry["url"].removesuffix(".git"):
            return name
    raise ValueError(f"No pinned dependency for {url}; use --dependency for a mirror URL")


def check_sail_version(executable: str, env: dict[str, str] | None = None) -> str:
    expected = load_lock()["sail_model_version"]
    path = shutil.which(executable, path=(env or os.environ).get("PATH")) or executable
    try:
        result = subprocess.run([path, "--version"], env=env, text=True,
                                capture_output=True, timeout=10, check=True)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"Cannot check Sail reference model {path}: {error}") from error
    found = result.stdout.strip()
    if found != expected:
        raise RuntimeError(
            f"Sail reference model version mismatch: pinned ACT4 requires {expected}, "
            f"but {path} reports {found!r}. Run .load_sail_riscv_sim.sh to install "
            "the locked ACT4/Sail pair, and check RISCV_HOME/TRIBE_ARCH_TEST_REF_MODEL."
        )
    print(f"Sail reference model: {path} ({found})", flush=True)
    return found


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "value":
        print(load_lock()[sys.argv[2]])
    elif len(sys.argv) == 3 and sys.argv[1] == "check-sail":
        try:
            check_sail_version(sys.argv[2])
        except RuntimeError as error:
            print(f"ERROR: {error}", file=sys.stderr)
            sys.exit(1)
    else:
        sys.exit("usage: riscv_dependencies.py value <key> | check-sail <executable>")
