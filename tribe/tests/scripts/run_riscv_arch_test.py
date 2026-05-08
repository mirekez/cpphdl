#!/usr/bin/env python3
"""Ensure riscv-arch-test is available and run configured RV32 tests."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys


SKIP = 77
REPO = "https://github.com/riscv-non-isa/riscv-arch-test.git"


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: run_riscv_arch_test.py <checkout-dir> <work-dir>", file=sys.stderr)
        return 2

    checkout = pathlib.Path(argv[1]).resolve()
    work = pathlib.Path(argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    ensure = pathlib.Path(__file__).with_name("ensure_git_repo.py")
    subprocess.run([sys.executable, str(ensure), str(checkout), REPO, "tests"], check=True)

    if shutil.which("make") is None:
        print("SKIP: make is not available")
        return SKIP

    gcc = os.environ.get("TRIBE_ARCH_TEST_GCC", "riscv32-unknown-elf-gcc")
    objdump = os.environ.get("TRIBE_ARCH_TEST_OBJDUMP", "riscv32-unknown-elf-objdump")
    if shutil.which(gcc) is None:
        print(f"SKIP: {gcc} is not available")
        return SKIP
    if shutil.which(objdump) is None:
        print(f"SKIP: {objdump} is not available")
        return SKIP

    if shutil.which("uv") is None and shutil.which("mise") is None:
        print("SKIP: riscv-arch-test requires uv or mise")
        return SKIP

    config = os.environ.get(
        "TRIBE_ARCH_TEST_CONFIG",
        str(checkout / "config" / "spike" / "spike-RVI20U32" / "test_config.yaml"),
    )
    extensions = os.environ.get(
        "TRIBE_ARCH_TEST_EXTENSIONS",
        "I,M,Zicsr,Zifencei,Zca",
    )
    exclude = os.environ.get(
        "TRIBE_ARCH_TEST_EXCLUDE_EXTENSIONS",
        "F,D,Zcf,Zcd,Zaamo,Zalrsc,Zicntr,Zihpm,Misalign,MisalignZca",
    )

    env = os.environ.copy()
    env["PATH"] = "/home/me/riscv/bin:" + env.get("PATH", "")

    overlay = work / "tribe-rv32-config"
    overlay.mkdir(parents=True, exist_ok=True)
    source_config = pathlib.Path(config)
    config_text = source_config.read_text(encoding="utf-8")
    config_text = config_text.replace("compiler_exe: riscv64-unknown-elf-gcc", f"compiler_exe: {gcc}")
    config_text = config_text.replace("objdump_exe: riscv64-unknown-elf-objdump", f"objdump_exe: {objdump}")
    local_config = overlay / "test_config.yaml"
    local_config.write_text(config_text, encoding="utf-8")
    for name in ("spike-RVI20U32.yaml", "link.ld", "rvtest_config.h", "rvtest_config.svh", "sail.json"):
        src = source_config.with_name(name)
        if src.exists():
            shutil.copy2(src, overlay / name)

    cmd = [
        "make",
        "-j",
        str(os.cpu_count() or 1),
        f"CONFIG_FILES={local_config}",
        f"WORKDIR={work}",
        f"EXTENSIONS={extensions}",
        f"EXCLUDE_EXTENSIONS={exclude}",
        "FAST=True",
    ]
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=checkout, env=env).returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
