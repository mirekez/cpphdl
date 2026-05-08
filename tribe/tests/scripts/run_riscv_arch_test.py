#!/usr/bin/env python3
"""Ensure riscv-arch-test is available and run configured RV32 tests."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys


REPO = "https://github.com/riscv-non-isa/riscv-arch-test.git"


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


def write_tribe_rvmodel_macros(path: pathlib.Path) -> None:
    path.write_text(
        r"""#ifndef _RVMODEL_MACROS_H
#define _RVMODEL_MACROS_H

#define RVMODEL_DATA_SECTION \
        .pushsection .tohost,"aw",@progbits;                \
        .align 8; .global tohost; tohost: .dword 0;         \
        .align 8; .global fromhost; fromhost: .dword 0;     \
        .popsection

#define RVMODEL_BOOT_TO_MMODE

#define RVMODEL_HALT_PASS  \
  li x1, 1                ;\
  la t0, tohost           ;\
  write_tohost_pass:      ;\
    sw x1, 0(t0)          ;\
    sw x0, 4(t0)          ;\
    j write_tohost_pass   ;

#define RVMODEL_HALT_FAIL \
  li x1, 3                ;\
  la t0, tohost           ;\
  write_tohost_fail:      ;\
    sw x1, 0(t0)          ;\
    sw x0, 4(t0)          ;\
    j write_tohost_fail   ;

#define RVMODEL_IO_INIT(_R1, _R2, _R3)
#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR)

#define RVMODEL_ACCESS_FAULT_ADDRESS 0x00000000
#define RVMODEL_MTIME_ADDRESS 0x0200BFF8
#define RVMODEL_MTIMECMP_ADDRESS 0x02004000
#define RVMODEL_INTERRUPT_LATENCY 10
#define RVMODEL_TIMER_INT_SOON_DELAY 100
#define RVMODEL_SET_MEXT_INT(_R1, _R2)
#define RVMODEL_CLR_MEXT_INT(_R1, _R2)
#define RVMODEL_SET_MSW_INT(_R1, _R2)
#define RVMODEL_CLR_MSW_INT(_R1, _R2)
#define RVMODEL_SET_SEXT_INT(_R1, _R2)
#define RVMODEL_CLR_SEXT_INT(_R1, _R2)
#define RVMODEL_SET_SSW_INT(_R1, _R2)
#define RVMODEL_CLR_SSW_INT(_R1, _R2)

#endif
""",
        encoding="utf-8",
    )


def write_tribe_rvtest_config(path: pathlib.Path) -> None:
    path.write_text(
        """#define RVMODEL_PMP_GRAIN 4
#define RVMODEL_NUM_PMPS 0

#define ZCA_SUPPORTED
""",
        encoding="utf-8",
    )


def prewarm_udb_z3_cache(env: dict[str, str]) -> None:
    dest = pathlib.Path(env["XDG_CACHE_HOME"]) / "udb" / "z3"
    source = pathlib.Path.home() / ".cache" / "udb" / "z3"
    if dest.resolve() == source.resolve() or dest.exists() or not source.exists():
        return
    shutil.copytree(source, dest, dirs_exist_ok=True)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: run_riscv_arch_test.py <tribe-bin> <checkout-dir> <work-dir>", file=sys.stderr)
        return 2

    tribe = pathlib.Path(argv[1]).resolve()
    checkout = pathlib.Path(argv[2]).resolve()
    work = pathlib.Path(argv[3]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    ensure = pathlib.Path(__file__).with_name("ensure_git_repo.py")
    subprocess.run([sys.executable, str(ensure), str(checkout), REPO, "tests"], check=True)

    env = os.environ.copy()
    riscv_home = env.get("RISCV_HOME", "/home/me/riscv")
    env["PATH"] = f"{riscv_home}/bin:" + env.get("PATH", "")
    env.setdefault("RISCV", riscv_home)
    env.setdefault("UV_CACHE_DIR", str(work / "uv-cache"))
    env.setdefault("XDG_DATA_HOME", str(work / "xdg-data"))
    env.setdefault("XDG_CACHE_HOME", str(work / "xdg-cache"))
    pathlib.Path(env["XDG_DATA_HOME"]).mkdir(parents=True, exist_ok=True)
    pathlib.Path(env["XDG_CACHE_HOME"]).mkdir(parents=True, exist_ok=True)
    prewarm_udb_z3_cache(env)

    if shutil.which("make", path=env["PATH"]) is None:
        print("ERROR: make is not available")
        return 1

    gcc = os.environ.get("TRIBE_ARCH_TEST_GCC", "riscv32-unknown-elf-gcc")
    objdump = os.environ.get("TRIBE_ARCH_TEST_OBJDUMP", "riscv32-unknown-elf-objdump")
    ref_model = os.environ.get("TRIBE_ARCH_TEST_REF_MODEL", "sail_riscv_sim")
    if shutil.which(gcc, path=env["PATH"]) is None:
        print(f"ERROR: {gcc} is not available")
        return 1
    if shutil.which(objdump, path=env["PATH"]) is None:
        print(f"ERROR: {objdump} is not available")
        return 1
    if shutil.which("riscv32-unknown-elf-readelf", path=env["PATH"]) is None:
        print("ERROR: riscv32-unknown-elf-readelf is not available")
        return 1
    if not tribe.exists():
        print(f"ERROR: Tribe binary not found: {tribe}")
        return 1

    backend = os.environ.get("TRIBE_ARCH_TEST_BACKEND", "cpphdl")
    if backend not in ("cpphdl", "verilator"):
        print(f"unsupported TRIBE_ARCH_TEST_BACKEND={backend!r}")
        return 2

    tribe_runner = tribe
    tribe_base_args: list[str] = ["--noveril"]
    if backend == "verilator":
        verilator_bin = pathlib.Path(
            os.environ.get("TRIBE_ARCH_TEST_VERILATOR_BIN", tribe.parent / "Tribe" / "obj_dir" / "VTribe")
        )
        if run([str(tribe), "1"], tribe.parent, env) != 0:
            print("failed to build Verilator Tribe model")
            return 1
        if not verilator_bin.exists():
            print(f"Verilator Tribe binary not found: {verilator_bin}")
            return 1
        tribe_runner = verilator_bin
        tribe_base_args = []

    if shutil.which(ref_model, path=env["PATH"]) is None:
        print(f"ERROR: {ref_model} is not available")
        return 1

    if shutil.which("uv", path=env["PATH"]) is None and shutil.which("mise", path=env["PATH"]) is None:
        print("ERROR: riscv-arch-test requires uv or mise")
        return 1
    if shutil.which("mise", path=env["PATH"]) is None and shutil.which("bundle", path=env["PATH"]) is None:
        print("ERROR: riscv-arch-test requires bundle when mise is not available")
        return 1

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

    overlay = work / "tribe-rv32-config"
    overlay.mkdir(parents=True, exist_ok=True)
    source_config = pathlib.Path(config)
    config_text = source_config.read_text(encoding="utf-8")
    config_text = config_text.replace("compiler_exe: riscv64-unknown-elf-gcc", f"compiler_exe: {gcc}")
    config_text = config_text.replace("objdump_exe: riscv64-unknown-elf-objdump", f"objdump_exe: {objdump}")
    config_text = config_text.replace("ref_model_exe: sail_riscv_sim", f"ref_model_exe: {ref_model}")
    local_config = overlay / "test_config.yaml"
    local_config.write_text(config_text, encoding="utf-8")
    for name in ("spike-RVI20U32.yaml", "link.ld", "rvtest_config.h", "rvtest_config.svh", "sail.json"):
        src = source_config.with_name(name)
        if src.exists():
            shutil.copy2(src, overlay / name)
    write_tribe_rvmodel_macros(overlay / "rvmodel_macros.h")
    write_tribe_rvtest_config(overlay / "rvtest_config.h")

    cmd = [
        "make",
        "-j",
        str(os.cpu_count() or 1),
        f"CONFIG_FILES={local_config}",
        f"WORKDIR={work}",
        f"EXTENSIONS={extensions}",
        f"EXCLUDE_EXTENSIONS={exclude}",
        f"JOBS={os.environ.get('TRIBE_ARCH_TEST_JOBS', '1')}",
        "FAST=True",
    ]
    if run(cmd, checkout, env) != 0:
        return 1

    pattern = os.environ.get("TRIBE_ARCH_TEST_PATTERN")
    elfs = sorted(path for path in work.glob("*/elfs/**/*.elf") if path.is_file())
    if pattern:
        import fnmatch
        patterns = pattern.split()
        elfs = [path for path in elfs if any(fnmatch.fnmatch(path.name, item) for item in patterns)]
    if not elfs:
        print(f"ERROR: no riscv-arch-test ELFs found in {work}")
        return 1

    cycles = os.environ.get("TRIBE_ARCH_TEST_CYCLES", "1000000")
    offset = os.environ.get("TRIBE_ARCH_TEST_OFFSET", "0x1000")
    addr_mask = int(os.environ.get("TRIBE_ARCH_TEST_ADDR_MASK", "0xffffffff"), 0)
    start_mem_addr = os.environ.get("TRIBE_ARCH_TEST_START_MEM_ADDR", "0x80000000")
    ram_size = os.environ.get("TRIBE_ARCH_TEST_RAM_SIZE", "131072")
    failed: list[str] = []
    for elf in elfs:
        print(f"== {elf.relative_to(work)} ==")
        tohost = symbol_addr(elf, "tohost", env)
        if tohost is None:
            print(f"missing tohost symbol in {elf}")
            failed.append(str(elf.relative_to(work)))
            continue
        result = subprocess.run(
            [
                str(tribe_runner),
                *tribe_base_args,
                "--program", str(elf),
                "--offset", offset,
                "--tohost", hex(tohost & addr_mask),
                "--start-mem-addr", start_mem_addr,
                "--ram-size", ram_size,
                "--cycles", cycles,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        )
        if result.stdout:
            print(result.stdout, end="")
        if result.returncode != 0:
            failed.append(str(elf.relative_to(work)))

    print(f"ran {len(elfs)} riscv-arch-test ELF(s) on Tribe {backend}")
    if failed:
        print("FAILED riscv-arch-test program(s): " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
