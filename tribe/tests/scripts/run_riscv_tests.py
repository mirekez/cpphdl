#!/usr/bin/env python3
"""Clone/build riscv-tests and run every RV32 ISA test with Spike and Tribe."""

from __future__ import annotations

import os
import pathlib
import fnmatch
import shutil
import subprocess
import sys


SKIP = 77
REPO = "https://github.com/riscv-software-src/riscv-tests.git"


def run(cmd: list[str], cwd: pathlib.Path, env: dict[str, str]) -> int:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=cwd, env=env).returncode


def symbol_addr(elf: pathlib.Path, symbol: str, env: dict[str, str]) -> int | None:
    result = subprocess.run(
        ["riscv32-unknown-elf-readelf", "-s", str(elf)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        return None
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields and fields[-1] == symbol:
            return int(fields[1], 16)
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: run_riscv_tests.py <tribe-bin> <checkout-dir> <work-dir>", file=sys.stderr)
        return 2

    tribe = pathlib.Path(argv[1]).resolve()
    checkout = pathlib.Path(argv[2]).resolve()
    work = pathlib.Path(argv[3]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    ensure = pathlib.Path(__file__).with_name("ensure_git_repo.py")
    subprocess.run([sys.executable, str(ensure), str(checkout), REPO, "isa"], check=True)
    subprocess.run(["git", "-C", str(checkout), "submodule", "update", "--init", "--recursive"], check=True)

    env = os.environ.copy()
    env["PATH"] = "/home/me/riscv/bin:" + env.get("PATH", "")
    env.setdefault("RISCV", "/home/me/riscv")

    missing = [tool for tool in ("autoconf", "automake", "make", "riscv32-unknown-elf-gcc", "riscv32-unknown-elf-readelf", "spike") if shutil.which(tool, path=env["PATH"]) is None]
    if missing:
        print("SKIP: missing external tool(s): " + ", ".join(missing))
        return SKIP
    if not tribe.exists():
        print(f"SKIP: Tribe binary not found: {tribe}")
        return SKIP

    if not (checkout / "configure").exists():
        if run(["autoconf"], checkout, env) != 0:
            return 1

    build = work / "build"
    build.mkdir(parents=True, exist_ok=True)
    makefile = build / "Makefile"
    if not makefile.exists() or "XLEN            := 32" not in makefile.read_text(encoding="utf-8"):
        if run([str(checkout / "configure"), "--with-xlen=32", f"--prefix={work / 'install'}"], build, env) != 0:
            return 1

    if run(["make", "-j", str(os.cpu_count() or 1), "isa"], build, env) != 0:
        return 1

    isa_dir = build / "isa"
    tests = sorted(
        path
        for path in isa_dir.glob("rv32*-p-*")
        if path.is_file() and not path.name.endswith((".dump", ".hex", ".bin"))
    )
    pattern = os.environ.get("TRIBE_RISCV_TESTS_PATTERN")
    if pattern:
        patterns = pattern.split()
        tests = [path for path in tests if any(fnmatch.fnmatch(path.name, item) for item in patterns)]
    if not tests:
        print(f"SKIP: no RV32 riscv-tests binaries found in {isa_dir}")
        return SKIP

    isa = os.environ.get(
        "TRIBE_RISCV_TESTS_ISA",
        "rv32gc_ziccid_zfh_zicboz_svnapot_zicntr_zba_zbb_zbkb_zbkx_zbc_zbs_zicond_zicclsm",
    )
    failed: list[str] = []
    mismatched: list[str] = []
    tribe_cycles = os.environ.get("TRIBE_RISCV_TESTS_CYCLES", "200000")
    tribe_offset = os.environ.get("TRIBE_RISCV_TESTS_OFFSET", "0x1000")
    addr_mask = int(os.environ.get("TRIBE_RISCV_TESTS_ADDR_MASK", "0x3fff"), 0)
    backend = os.environ.get("TRIBE_RISCV_TESTS_BACKEND", "cpphdl")
    if backend not in ("cpphdl", "verilator"):
        print(f"unsupported TRIBE_RISCV_TESTS_BACKEND={backend!r}")
        return 2

    tribe_runner = tribe
    tribe_base_args: list[str] = ["--noveril"]
    if backend == "verilator":
        verilator_bin = pathlib.Path(
            os.environ.get("TRIBE_RISCV_TESTS_VERILATOR_BIN", tribe.parent / "Tribe" / "obj_dir" / "VTribe")
        )
        compile_result = subprocess.run([str(tribe), "1"], cwd=tribe.parent, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        if compile_result.stdout:
            print(compile_result.stdout, end="")
        if compile_result.returncode != 0:
            print("failed to build Verilator Tribe model")
            return 1
        if not verilator_bin.exists():
            print(f"Verilator Tribe binary not found: {verilator_bin}")
            return 1
        tribe_runner = verilator_bin
        tribe_base_args = []

    for test in tests:
        print(f"== {test.name} ==")
        spike_result = subprocess.run(["spike", f"--isa={isa}", str(test)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        if spike_result.stdout:
            print(spike_result.stdout, end="")
        if spike_result.returncode != 0:
            failed.append(test.name)
            continue

        tohost = symbol_addr(test, "tohost", env)
        if tohost is None:
            print(f"missing tohost symbol in {test}")
            failed.append(test.name)
            continue

        tribe_result = subprocess.run(
            [
                str(tribe_runner),
                *tribe_base_args,
                "--program", str(test),
                "--offset", tribe_offset,
                "--tohost", hex(tohost & addr_mask),
                "--cycles", tribe_cycles,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        )
        if tribe_result.stdout:
            print(tribe_result.stdout, end="")
        if tribe_result.returncode != spike_result.returncode:
            mismatched.append(test.name)

    print(f"ran {len(tests)} RV32 riscv-tests binaries")
    if failed or mismatched:
        if failed:
            print("FAILED Spike/reference riscv-tests RV32 program(s): " + ", ".join(failed))
        if mismatched:
            print("FAILED Tribe/Spike mismatch RV32 program(s): " + ", ".join(mismatched))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
