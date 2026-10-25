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


def prepare_spike_runtime(env: dict[str, str], executable: str = "spike") -> None:
    """Set the reference process's library path and reject unusable binaries early."""
    prefix = env.get("CONDA_PREFIX")
    if prefix:
        library = str(pathlib.Path(prefix) / "lib")
        previous = env.get("LD_LIBRARY_PATH", "")
        paths = [path for path in previous.split(os.pathsep) if path and path != library]
        env["LD_LIBRARY_PATH"] = os.pathsep.join([library, *paths])
    path = shutil.which(executable, path=env.get("PATH")) or executable
    try:
        result = subprocess.run([path, "--help"], env=env, text=True,
                                capture_output=True, timeout=10)
        if result.returncode:
            raise RuntimeError(f"exit {result.returncode}: {result.stdout}{result.stderr}")
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        raise RuntimeError(
            f"Spike reference model cannot start: {path}\n{error}\n"
            "Run tribe_cpu/tests/.load_spike.sh in the active environment to rebuild "
            "Spike against its installed libraries. Check RISCV_HOME and PATH."
        ) from error


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
    elif len(sys.argv) == 3 and sys.argv[1] in ("check-sail", "check-spike"):
        try:
            if sys.argv[1] == "check-spike":
                prepare_spike_runtime(os.environ.copy(), sys.argv[2])
            else:
                check_sail_version(sys.argv[2])
        except RuntimeError as error:
            print(f"ERROR: {error}", file=sys.stderr)
            sys.exit(1)
    else:
        sys.exit("usage: riscv_dependencies.py value <key> | check-sail/check-spike <executable>")
