#!/usr/bin/env python3
"""Ensure riscv-dv is available and run broad RV32 generation when configured."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys


SKIP = 77
REPO = "https://github.com/google/riscv-dv.git"
PYTHON_DEPS = [
    "bitarray",
    "bitstring",
    "numpy",
    "pandas",
    "pyboolector",
    "pyucis",
    "pyvsc",
    "PyYAML",
    "tabulate",
    "toposort",
]


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
        print("usage: run_riscv_dv.py <tribe-bin> <checkout-dir> <work-dir>", file=sys.stderr)
        return 2

    tribe = pathlib.Path(argv[1]).resolve()
    checkout = pathlib.Path(argv[2]).resolve()
    work = pathlib.Path(argv[3]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    repo_root = pathlib.Path(__file__).resolve().parents[3]

    ensure = pathlib.Path(__file__).with_name("ensure_git_repo.py")
    subprocess.run([sys.executable, str(ensure), str(checkout), REPO, "run.py"], check=True)

    python = os.environ.get("TRIBE_RISCV_DV_PYTHON", "/usr/bin/python3")
    if shutil.which(python) is None:
        print("SKIP: python3 is not available")
        return SKIP

    env = os.environ.copy()
    riscv_home = env.get("RISCV_HOME", "/home/me/riscv")
    env["PATH"] = "/usr/bin:" + str(pathlib.Path(riscv_home) / "bin") + os.pathsep + env.get("PATH", "")
    env.setdefault("RISCV", riscv_home)
    pydeps = repo_root / "build" / "pydeps"
    pydeps.mkdir(parents=True, exist_ok=True)
    env["PYTHONPATH"] = str(pydeps) + os.pathsep + env.get("PYTHONPATH", "")

    if subprocess.run([python, "-c", "import vsc"], env=env).returncode != 0:
        print(f"Installing riscv-dv Python dependencies into {pydeps}")
        if subprocess.run([python, "-m", "pip", "install", "--target", str(pydeps), *PYTHON_DEPS], env=env).returncode != 0:
            return 1

    missing = [
        tool for tool in (
            "riscv32-unknown-elf-gcc",
            "riscv32-unknown-elf-objcopy",
            "riscv32-unknown-elf-readelf",
            "spike",
        )
        if shutil.which(tool, path=env["PATH"]) is None
    ]
    if missing:
        print("SKIP: missing external tool(s): " + ", ".join(missing))
        return SKIP
    if not tribe.exists():
        print(f"SKIP: Tribe binary not found: {tribe}")
        return SKIP

    tests = [
        test.strip()
        for test in os.environ.get(
            "TRIBE_RISCV_DV_TESTS",
            "tribe_arithmetic_basic_test",
        ).split(",")
        if test.strip()
    ]

    env["RISCV_GCC"] = os.environ.get("RISCV_GCC", str(pathlib.Path(riscv_home) / "bin" / "riscv32-unknown-elf-gcc"))
    env["RISCV_OBJCOPY"] = os.environ.get("RISCV_OBJCOPY", str(pathlib.Path(riscv_home) / "bin" / "riscv32-unknown-elf-objcopy"))
    env["SPIKE_PATH"] = os.environ.get("SPIKE_PATH", str(pathlib.Path(riscv_home) / "bin"))
    env["PYTHONPATH"] = (
        str(pydeps) + os.pathsep +
        str(checkout / "pygen") + os.pathsep +
        env.get("PYTHONPATH", "")
    )

    testlist = pathlib.Path(os.environ.get(
        "TRIBE_RISCV_DV_TESTLIST",
        repo_root / "tribe" / "tests" / "riscv_dv_tribe_testlist.yaml",
    )).resolve()
    target = os.environ.get("TRIBE_RISCV_DV_TARGET", "rv32imc")
    iterations = os.environ.get("TRIBE_RISCV_DV_ITERATIONS", "1")
    cycles = os.environ.get("TRIBE_RISCV_DV_CYCLES", "300000")
    offset = os.environ.get("TRIBE_RISCV_DV_OFFSET", "0x1000")
    addr_mask = int(os.environ.get("TRIBE_RISCV_DV_ADDR_MASK", "0x1ffff"), 0)
    isa = os.environ.get("TRIBE_RISCV_DV_ISA", "rv32imc_zicsr_zifencei")
    backend = os.environ.get("TRIBE_RISCV_DV_BACKEND", "cpphdl")
    if backend not in ("cpphdl", "verilator"):
        print(f"unsupported TRIBE_RISCV_DV_BACKEND={backend!r}")
        return 2

    tribe_runner = tribe
    tribe_base_args: list[str] = ["--noveril"]
    if backend == "verilator":
        verilator_bin = pathlib.Path(
            os.environ.get("TRIBE_RISCV_DV_VERILATOR_BIN", tribe.parent / "Tribe" / "obj_dir" / "VTribe")
        )
        if run([str(tribe), "1"], tribe.parent, env) != 0:
            print("failed to build Verilator Tribe model")
            return 1
        if not verilator_bin.exists():
            print(f"Verilator Tribe binary not found: {verilator_bin}")
            return 1
        tribe_runner = verilator_bin
        tribe_base_args = []

    failed = []
    mismatched = []
    for test in tests:
        test_work = work / test
        cmd = [
            python,
            str(checkout / "run.py"),
            "--target", target,
            "--simulator", "pyflow",
            "--testlist", str(testlist),
            "--test", test,
            "--iterations", iterations,
            "--output", str(test_work),
            "--steps", "gen,gcc_compile",
        ]
        if run(cmd, checkout, env) != 0:
            failed.append(test)
            continue

        elfs = sorted((test_work / "asm_test").glob(f"{test}_*.o"))
        if not elfs:
            print(f"no riscv-dv ELF outputs found for {test}")
            failed.append(test)
            continue

        for elf in elfs:
            print(f"== {elf.name} ==")
            spike_result = subprocess.run(["spike", f"--isa={isa}", str(elf)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            if spike_result.stdout:
                print(spike_result.stdout, end="")
            if spike_result.returncode != 0:
                failed.append(elf.name)
                continue

            tohost = symbol_addr(elf, "tohost", env)
            if tohost is None:
                print(f"missing tohost symbol in {elf}")
                failed.append(elf.name)
                continue

            tribe_result = subprocess.run(
                [
                    str(tribe_runner),
                    *tribe_base_args,
                    "--program", str(elf),
                    "--offset", offset,
                    "--tohost", hex(tohost & addr_mask),
                    "--cycles", cycles,
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
            )
            if tribe_result.stdout:
                print(tribe_result.stdout, end="")
            if tribe_result.returncode != spike_result.returncode:
                mismatched.append(elf.name)

    if failed or mismatched:
        if failed:
            print("FAILED riscv-dv RV32 program(s): " + ", ".join(failed))
        if mismatched:
            print("FAILED riscv-dv Tribe/Spike mismatch RV32 program(s): " + ", ".join(mismatched))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
