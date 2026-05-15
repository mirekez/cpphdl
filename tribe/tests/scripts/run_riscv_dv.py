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
    "wheel",
    "setuptools>=65",
    "bitarray",
    "bitstring",
    "numpy",
    "pandas",
    "pyboolector",
    "pyucis",
    "pyvsc",
    "PyYAML>=6.0",
    "requests>=2.31",
    "tabulate",
    "toposort",
]

DEFAULT_TESTLIST = """\
- test: tribe_arithmetic_basic_test
  description: >
    Short RV32IMC arithmetic/random corner test for Tribe smoke regression.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=80
    +num_of_sub_program=0
    +directed_instr_0=riscv_int_numeric_corner_stream,4
    +no_fence=1
    +no_data_page=1
    +no_branch_jump=1
    +boot_mode=m
    +no_csr_instr=1
  rtl_test: core_base_test

- test: tribe_amo_test
  description: >
    Short RV32A AMO/LR/SC directed test for Tribe.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=120
    +directed_instr_0=riscv_lr_sc_instr_stream,4
    +directed_instr_1=riscv_amo_instr_stream,6
    +no_fence=1
    +no_branch_jump=1
    +num_of_sub_program=0
    +boot_mode=m
    +no_csr_instr=1
  rtl_test: core_base_test

- test: tribe_trap_test
  description: >
    Short illegal-instruction trap regression for Tribe privileged trap flow.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=120
    +illegal_instr_ratio=8
    +num_of_sub_program=0
    +boot_mode=m
    +no_fence=1
    +no_data_page=1
  rtl_test: core_base_test

- test: tribe_interrupt_test
  description: >
    Short privileged interrupt-handler generation smoke for Tribe.
  gen_test: riscv_instr_base_test
  iterations: 1
  gen_opts: >
    +instr_cnt=100
    +enable_interrupt=1
    +enable_timer_irq=1
    +num_of_sub_program=0
    +boot_mode=m
    +no_fence=1
    +no_data_page=1
  rtl_test: core_base_test
"""


def run(cmd: list[str], cwd: pathlib.Path, env: dict[str, str]) -> int:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=cwd, env=env).returncode


def default_testlist(repo_root: pathlib.Path, work: pathlib.Path) -> pathlib.Path:
    source_testlist = repo_root / "tribe" / "tests" / "riscv_dv_tribe_testlist.yaml"
    if source_testlist.exists():
        return source_testlist

    generated_testlist = work / "riscv_dv_tribe_testlist.yaml"
    if not generated_testlist.exists():
        generated_testlist.write_text(DEFAULT_TESTLIST, encoding="utf-8")
    return generated_testlist


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


def ensure_tribe_atomic_target(checkout: pathlib.Path) -> str:
    target = "tribe_rv32imac_a"
    source = checkout / "pygen" / "pygen_src" / "target" / "rv32imc"
    dest = checkout / "pygen" / "pygen_src" / "target" / target
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "riscvOVPsim.ic", dest / "riscvOVPsim.ic")
    text = (source / "riscv_core_setting.py").read_text(encoding="utf-8")
    text = text.replace(
        "supported_isa = [riscv_instr_group_t.RV32I, riscv_instr_group_t.RV32M, riscv_instr_group_t.RV32C]",
        "supported_isa = [riscv_instr_group_t.RV32I, riscv_instr_group_t.RV32M, "
        "riscv_instr_group_t.RV32A, riscv_instr_group_t.RV32C]",
    )
    (dest / "riscv_core_setting.py").write_text(text, encoding="utf-8")
    return target


def git_head_file(checkout: pathlib.Path, relpath: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(checkout), "show", f"HEAD:{relpath}"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr)
    return result.stdout


def patch_riscv_dv_amo_pygen(checkout: pathlib.Path) -> None:
    path = checkout / "pygen" / "pygen_src" / "riscv_amo_instr_lib.py"
    text = git_head_file(checkout, "pygen/pygen_src/riscv_amo_instr_lib.py")
    text = text.replace(
        "        # User can specify a small group of available registers to generate various hazard condition\n"
        "        self.avail_regs = vsc.randsz_list_t(vsc.enum_t(riscv_reg_t))\n",
        "        # avail_regs is inherited from riscv_instr_stream.\n",
    )
    text = text.replace(
        "        self.data_page_id = random.randrange(0, max_data_page_id - 1)\n",
        "        self.data_page_id = random.randrange(0, max_data_page_id)\n",
    )
    text = text.replace(
        "        self.reserved_rd.append(self.rs1_reg)\n",
        "        self.reserved_rd.append(self.rs1_reg[0])\n",
    )
    text = text.replace(
        "        self.num_mixed_instr in vsc.rangelist(vsc.rng(0, 15))\n",
        "        self.num_mixed_instr == 0\n",
    )
    text = text.replace(
        "        self.num_amo in vsc.rangelist(vsc.rng(1, 10))\n"
        "        self.num_mixed_instr in vsc.rangelist(vsc.rng(0, self.num_amo))\n",
        "        self.num_amo in vsc.rangelist(vsc.rng(1, 4))\n"
        "        self.num_mixed_instr == 0\n",
    )
    text = text.replace(
        "            self.amo_instr.append(riscv_instr.get_rand_instr(\n"
        "                                  include_category=[riscv_instr_category_t.AMO]))\n",
        "            self.amo_instr.append(riscv_instr.get_rand_instr(include_instr=[\n"
        "                                  riscv_instr_name_t.AMOSWAP_W,\n"
        "                                  riscv_instr_name_t.AMOADD_W,\n"
        "                                  riscv_instr_name_t.AMOAND_W,\n"
        "                                  riscv_instr_name_t.AMOOR_W,\n"
        "                                  riscv_instr_name_t.AMOXOR_W,\n"
        "                                  riscv_instr_name_t.AMOMIN_W,\n"
        "                                  riscv_instr_name_t.AMOMAX_W,\n"
        "                                  riscv_instr_name_t.AMOMINU_W,\n"
        "                                  riscv_instr_name_t.AMOMAXU_W]))\n",
    )
    text = text.replace(
        "                self.amo_instr[i].rd.inside(vsc.rangelist(self.rs1_reg))\n",
        "                # Tribe patch: rd does not need to alias the AMO address register.\n",
    )
    text = text.replace(
        "            self.instr_list.insert(0, self.amo_instr[i])\n",
        "            self.amo_instr[i].rs1 = self.rs1_reg[0]\n"
        "            self.amo_instr[i].rd = riscv_reg_t.ZERO\n"
        "            self.instr_list.insert(0, self.amo_instr[i])\n",
    )
    text = text.replace(
        "        self.instr_list.extend((self.lr_instr, self.sc_instr))\n",
        "        self.lr_instr.rs1 = self.rs1_reg[0]\n"
        "        self.sc_instr.rs1 = self.rs1_reg[0]\n"
        "        self.instr_list.extend((self.lr_instr, self.sc_instr))\n",
    )
    path.write_text(text, encoding="utf-8")

    asm_path = checkout / "pygen" / "pygen_src" / "riscv_asm_program_gen.py"
    text = git_head_file(checkout, "pygen/pygen_src/riscv_asm_program_gen.py")
    text = text.replace(
        "            self.callstack_gen.init(num_sub_program + 1)\n",
        "            callstack_gen.init(num_sub_program + 1)\n",
    )
    asm_path.write_text(text, encoding="utf-8")

    callstack_path = checkout / "pygen" / "pygen_src" / "riscv_callstack_gen.py"
    text = git_head_file(checkout, "pygen/pygen_src/riscv_callstack_gen.py")
    text = text.replace(
        "            self.program_h[i] = riscv_program(\"program_{}\".format(i))\n",
        "            self.program_h[i] = riscv_program()\n",
    )
    callstack_path.write_text(text, encoding="utf-8")


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
    patch_riscv_dv_amo_pygen(checkout)

    python = os.environ.get("TRIBE_RISCV_DV_PYTHON", "/usr/bin/python3")
    if shutil.which(python) is None:
        print("SKIP: python3 is not available")
        return SKIP

    env = os.environ.copy()
    riscv_home = env.get("RISCV_HOME") or env.get("RISCV") or "/home/me/riscv"
    env["RISCV_HOME"] = riscv_home
    env["PATH"] = "/usr/bin:" + str(pathlib.Path(riscv_home) / "bin") + os.pathsep + env.get("PATH", "")
    env.setdefault("RISCV", riscv_home)
    pydeps = repo_root / "build" / "pydeps"
    pydeps.mkdir(parents=True, exist_ok=True)
    env["PYTHONPATH"] = str(pydeps) + os.pathsep + env.get("PYTHONPATH", "")

    dep_check = (
        "import requests, vsc, yaml\n"
        "def vt(s): return tuple(int(p) for p in s.split('.')[:2] if p.isdigit())\n"
        "assert vt(yaml.__version__) >= (6, 0), yaml.__version__\n"
        "assert vt(requests.__version__) >= (2, 31), requests.__version__\n"
    )
    if subprocess.run([python, "-c", dep_check], env=env).returncode != 0:
        print(f"Installing riscv-dv Python dependencies into {pydeps}")
        if subprocess.run([python, "-m", "pip", "install",
                           "--target", str(pydeps),
                           "--upgrade",
                           "--no-warn-conflicts",
                           *PYTHON_DEPS], env=env).returncode != 0:
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
            "tribe_arithmetic_basic_test,tribe_amo_test,tribe_trap_test,tribe_interrupt_test",
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
        default_testlist(repo_root, work),
    )).resolve()
    target = os.environ.get("TRIBE_RISCV_DV_TARGET", ensure_tribe_atomic_target(checkout))
    iterations = os.environ.get("TRIBE_RISCV_DV_ITERATIONS", "1")
    cycles = os.environ.get("TRIBE_RISCV_DV_CYCLES", "300000")
    spike_timeout = int(os.environ.get("TRIBE_RISCV_DV_SPIKE_TIMEOUT", "30"))
    skip_spike_interrupt = os.environ.get("TRIBE_RISCV_DV_INTERRUPT_NO_SPIKE", "1") != "0"
    offset = os.environ.get("TRIBE_RISCV_DV_OFFSET", "0x1000")
    start_mem_addr = os.environ.get("TRIBE_RISCV_DV_START_MEM_ADDR", "0x80000000")
    addr_mask = int(os.environ.get("TRIBE_RISCV_DV_ADDR_MASK", "0xffffffff"), 0)
    isa = os.environ.get("TRIBE_RISCV_DV_ISA", "rv32imac_zicsr_zifencei")
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
        target_dir = checkout / "pygen" / "pygen_src" / "target" / target
        cmd = [
            python,
            str(checkout / "run.py"),
            "--target", target,
            "--custom_target", str(target_dir),
            "--isa", isa,
            "--mabi", "ilp32",
            "--sim_opts", os.environ.get("TRIBE_RISCV_DV_SIM_OPTS", ""),
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
            run_spike_reference = not (skip_spike_interrupt and "interrupt" in test)
            spike_returncode = 0
            if run_spike_reference:
                try:
                    spike_result = subprocess.run(
                        ["spike", f"--isa={isa}", str(elf)],
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        env=env,
                        timeout=spike_timeout,
                    )
                except subprocess.TimeoutExpired as exc:
                    print(f"Spike timed out after {spike_timeout}s for {elf.name}")
                    if exc.stdout:
                        print(exc.stdout, end="")
                    failed.append(elf.name)
                    continue
                if spike_result.stdout:
                    print(spike_result.stdout, end="")
                spike_returncode = spike_result.returncode
                if spike_returncode != 0:
                    failed.append(elf.name)
                    continue
            else:
                print("Skipping Spike reference for riscv-dv interrupt program; generated IRQ setup is target-driven.")

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
                    "--start-mem-addr", start_mem_addr,
                    "--cycles", cycles,
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
            )
            if tribe_result.stdout:
                print(tribe_result.stdout, end="")
            if tribe_result.returncode != spike_returncode:
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
