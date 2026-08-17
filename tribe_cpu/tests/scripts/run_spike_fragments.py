#!/usr/bin/env python3
"""Run broad RV32 instruction fragments through Spike and Tribe."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import textwrap


SKIP = 77
MMIO_OUT = "0x11223344"


class Fragment:
    def __init__(self, name: str, march: str, spike_isa: str, body: str):
        self.name = name
        self.march = march
        self.spike_isa = spike_isa
        self.body = body


def tool(name: str) -> str | None:
    return shutil.which(name)


def run(cmd: list[str], cwd: pathlib.Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd))
    result = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.stdout:
        print(result.stdout, end="")
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd, output=result.stdout)
    return result


def write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip(), encoding="utf-8")


def asm_source(name: str, body: str) -> str:
    return f"""
        .section .text
        .globl _start
        _start:
            {body}
        pass:
        #ifdef SPIKE_REFERENCE
            ebreak
        #else
            la a0, pass_msg
            jal ra, puts
        done:
            j done
        #endif
        fail:
        #ifdef SPIKE_REFERENCE
            unimp
        #else
            la a0, fail_msg
            jal ra, puts
        fail_loop:
            j fail_loop
        #endif

        #ifndef SPIKE_REFERENCE
        puts:
            li t6, {MMIO_OUT}
        puts_loop:
            lbu t5, 0(a0)
            beqz t5, puts_done
            sw t5, 0(t6)
            addi a0, a0, 1
            j puts_loop
        puts_done:
            ret

        .section .rodata
        pass_msg:
            .asciz "{name}\\n"
        fail_msg:
            .asciz "FAIL {name}\\n"
        #endif
    """


FRAGMENTS = [
    Fragment(
        "RV32I",
        "rv32i_zicsr",
        "RV32I_Zicsr",
        """
            li s0, 0x100
            addi s1, zero, 17
            addi s2, zero, -5
            add s3, s1, s2
            addi t0, zero, 12
            bne s3, t0, fail
            sub s3, s1, s2
            addi t0, zero, 22
            bne s3, t0, fail
            xori s3, s1, 0x55
            li t0, 0x44
            bne s3, t0, fail
            ori s3, s1, 0x40
            li t0, 0x51
            bne s3, t0, fail
            andi s3, s3, 0x11
            bne s3, s1, fail
            slli s3, s1, 3
            li t0, 136
            bne s3, t0, fail
            srli s3, s3, 2
            li t0, 34
            bne s3, t0, fail
            li t1, -16
            srai s3, t1, 2
            li t0, -4
            bne s3, t0, fail
            slti s3, t1, -1
            li t0, 1
            bne s3, t0, fail
            sltiu s3, t1, 1
            bne s3, zero, fail

            la t2, data_word
            sw s1, 0(t2)
            lw s3, 0(t2)
            bne s3, s1, fail
            li t0, 0x7f
            sb t0, 4(t2)
            lbu s3, 4(t2)
            bne s3, t0, fail
            li t0, -2
            sh t0, 6(t2)
            lh s3, 6(t2)
            bne s3, t0, fail

            beq zero, zero, 1f
            j fail
        1:
            bne zero, s1, 2f
            j fail
        2:
            blt s2, s1, 3f
            j fail
        3:
            bge s1, s2, 4f
            j fail
        4:
            bltu zero, s1, 5f
            j fail
        5:
            bgeu s1, zero, 6f
            j fail
        6:
            jal ra, 7f
            j fail
        7:
            la t0, 8f
            jalr zero, 0(t0)
            j fail
        8:
            j pass

        .balign 4
        data_word:
            .word 0
            .word 0
        """,
    ),
    Fragment(
        "RV32M",
        "rv32im_zicsr",
        "RV32IM_Zicsr",
        """
            li s0, 7
            li s1, -3
            mul s2, s0, s1
            li t0, -21
            bne s2, t0, fail
            mulh s2, s1, s1
            li t0, 0
            bne s2, t0, fail
            mulhu s2, s1, s1
            li t0, -6
            bne s2, t0, fail
            li s3, 22
            div s2, s3, s0
            li t0, 3
            bne s2, t0, fail
            rem s2, s3, s0
            li t0, 1
            bne s2, t0, fail
            divu s2, s3, s0
            li t0, 3
            bne s2, t0, fail
            remu s2, s3, s0
            li t0, 1
            bne s2, t0, fail
            div s2, s3, zero
            li t0, -1
            bne s2, t0, fail
            rem s2, s3, zero
            bne s2, s3, fail
            j pass
        """,
    ),
    Fragment(
        "RV32C",
        "rv32imc_zicsr",
        "RV32IMC_Zicsr",
        """
            .option push
            .option rvc
            c.li s0, 5
            c.addi s0, 7
            c.li s1, 12
            bne s0, s1, fail
            c.addi s1, -2
            c.sub s0, s1
            c.li t0, 2
            bne s0, t0, fail
            c.mv s2, s0
            c.add s2, s1
            c.li t0, 12
            bne s2, t0, fail
            c.bnez s0, 1f
            j fail
        1:
            c.li s1, 0
            c.beqz s1, 2f
            j fail
        2:
            c.j 1f
            j fail
        1:
            c.jal 3f
            j fail
        3:
            la s1, c_data
            sw s0, 0(s1)
            c.lw s1, 0(s1)
            bne s1, s0, fail
            la s1, c_data
            c.sw s0, 4(s1)
            lw s1, 4(s1)
            bne s1, s0, fail
            .option pop
            j pass

        .balign 4
        c_data:
            .word 0
            .word 0
        """,
    ),
    Fragment(
        "Zicsr",
        "rv32im_zicsr",
        "RV32IM_Zicsr",
        """
            li t0, 0x5a
            csrw mscratch, t0
            csrr t1, mscratch
            bne t1, t0, fail
            li t2, 0x01
            csrrs t3, mscratch, t2
            bne t3, t0, fail
            csrr t4, mscratch
            li t5, 0x5b
            bne t4, t5, fail
            csrrc t3, mscratch, t2
            bne t3, t5, fail
            csrr t4, mscratch
            bne t4, t0, fail
            csrrwi t3, mscratch, 7
            bne t3, t0, fail
            csrr t4, mscratch
            li t5, 7
            bne t4, t5, fail
            j pass
        """,
    ),
    Fragment(
        "Traps",
        "rv32im_zicsr",
        "RV32IM_Zicsr",
        """
            la t0, trap_vector
            csrw mtvec, t0
            csrw mscratch, zero
            nop
            nop

        illegal_site:
            .word 0xffffffff
        after_illegal:
            csrr t1, mscratch
            li t2, 1
            bne t1, t2, fail

        ebreak_site:
            ebreak
        after_ebreak:
            csrr t1, mscratch
            li t2, 2
            bne t1, t2, fail
            j pass

        trap_vector:
            csrr t3, mcause
            csrr t4, mepc
            li t5, 2
            beq t3, t5, handle_illegal
            li t5, 3
            beq t3, t5, handle_break
            j fail

        handle_illegal:
            la t5, illegal_site
            bne t4, t5, fail
            li t6, 1
            csrw mscratch, t6
            la t5, after_illegal
            csrw mepc, t5
            nop
            nop
            mret

        handle_break:
            la t5, ebreak_site
            bne t4, t5, fail
            li t6, 2
            csrw mscratch, t6
            la t5, after_ebreak
            csrw mepc, t5
            nop
            nop
            mret
        """,
    ),
    Fragment(
        "CLINT",
        "rv32im_zicsr",
        "RV32IM_Zicsr",
        """
            #ifdef SPIKE_REFERENCE
                li s0, 0x02000000
            #else
                li s0, 0x00070100
            #endif
            li s1, 0x4000
            add s1, s0, s1          # mtimecmp
            li s2, 0xBFF8
            add s2, s0, s2          # mtime

            la t0, mtimer_handler
            csrw mtvec, t0
            csrw mscratch, zero
            li t0, -1
            sw t0, 0(s1)
            sw t0, 4(s1)
            csrw mip, zero
            li t0, 0x80             # mie.MTIE
            csrw mie, t0
            csrsi mstatus, 8        # mstatus.MIE
            lw t1, 0(s2)
            addi t1, t1, 32
            sw t1, 0(s1)
            sw zero, 4(s1)

            li t2, 10000
        wait_timer:
            csrr t3, mscratch
            bnez t3, timer_seen
            addi t2, t2, -1
            bnez t2, wait_timer
            j fail

        timer_seen:
            li t4, 1
            bne t3, t4, fail
            j pass

        mtimer_handler:
            csrr t5, mcause
            li t6, 0x80000007
            bne t5, t6, fail
            li t6, 1
            csrw mscratch, t6
            li t6, -1
            sw t6, 0(s1)
            sw t6, 4(s1)
            mret
        """,
    ),
    Fragment(
        "RV32A",
        "rv32ima_zicsr",
        "RV32IMA_Zicsr",
        """
            la s0, atomic_word
            li s1, 0x10
            sw s1, 0(s0)

            lr.w s2, (s0)
            bne s2, s1, fail
            li s3, 0x20
            sc.w s4, s3, (s0)
            bnez s4, fail
            lw s5, 0(s0)
            bne s5, s3, fail

            li s1, 5
            amoadd.w s2, s1, (s0)
            li t0, 0x20
            bne s2, t0, fail
            lw s5, 0(s0)
            li t0, 0x25
            bne s5, t0, fail

            li s1, 0x55
            amoswap.w s2, s1, (s0)
            li t0, 0x25
            bne s2, t0, fail
            lw s5, 0(s0)
            bne s5, s1, fail

            li s1, 0x0f
            amoand.w s2, s1, (s0)
            li t0, 0x55
            bne s2, t0, fail
            lw s5, 0(s0)
            li t0, 0x05
            bne s5, t0, fail

            li s1, 0x80
            amoor.w s2, s1, (s0)
            li t0, 0x05
            bne s2, t0, fail
            lw s5, 0(s0)
            li t0, 0x85
            bne s5, t0, fail

            li s1, 0xff
            amoxor.w s2, s1, (s0)
            li t0, 0x85
            bne s2, t0, fail
            lw s5, 0(s0)
            li t0, 0x7a
            bne s5, t0, fail

            li s1, -1
            amomin.w s2, s1, (s0)
            li t0, 0x7a
            bne s2, t0, fail
            lw s5, 0(s0)
            bne s5, s1, fail

            li s1, 7
            amomax.w s2, s1, (s0)
            li t0, -1
            bne s2, t0, fail
            lw s5, 0(s0)
            bne s5, s1, fail

            li s1, 3
            amominu.w s2, s1, (s0)
            li t0, 7
            bne s2, t0, fail
            lw s5, 0(s0)
            bne s5, s1, fail

            li s1, 9
            amomaxu.w s2, s1, (s0)
            li t0, 3
            bne s2, t0, fail
            lw s5, 0(s0)
            bne s5, s1, fail

            j pass

        .balign 4
        atomic_word:
            .word 0
        """,
    ),
    Fragment(
        "Zifencei",
        "rv32im_zicsr_zifencei",
        "RV32IM_Zicsr_Zifencei",
        """
            fence.i
            fence
            j pass
        """,
    ),
]


def run_fragment(
    fragment: Fragment,
    gcc: str,
    objcopy: str,
    spike: str,
    tribe_runner: pathlib.Path,
    tribe_base_args: list[str],
    work: pathlib.Path,
) -> int:
    src = work / f"{fragment.name}.S"
    elf = work / f"{fragment.name}.elf"
    bin_file = work / f"{fragment.name}.bin"
    expected = work / f"{fragment.name}.log"

    write(src, asm_source(fragment.name, fragment.body))
    expected.write_text(f"{fragment.name}\n", encoding="ascii")

    spike_elf = work / f"{fragment.name}.spike.elf"
    run([
        gcc,
        f"-march={fragment.march}",
        "-mabi=ilp32",
        "-DSPIKE_REFERENCE",
        "-nostdlib",
        "-nostartfiles",
        "-Wl,-Ttext=0x80001000",
        str(src),
        "-o",
        str(spike_elf),
    ])
    spike_result = run([spike, "--instructions=10000", f"--isa={fragment.spike_isa}", str(spike_elf)], check=False)
    print(spike_result.stdout)
    if "illegal_instruction" in spike_result.stdout or "trap" in spike_result.stdout or "unimp" in spike_result.stdout:
        print(f"Spike reference failed for {fragment.name}")
        return 1

    compile_cmd = [
        gcc,
        f"-march={fragment.march}",
        "-mabi=ilp32",
        "-nostdlib",
        "-nostartfiles",
        "-Wl,-Ttext=0",
        str(src),
        "-o",
        str(elf),
    ]
    run(compile_cmd)

    run([objcopy, "-O", "binary", str(elf), str(bin_file)])
    tribe_result = run(
        [str(tribe_runner), *tribe_base_args, "--program", str(bin_file), "--offset", "0", "--log", str(expected)],
        cwd=work,
        check=False,
    )
    print(tribe_result.stdout)
    return 0 if tribe_result.returncode == 0 else 1


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: run_spike_fragments.py <tribe-binary> <work-dir>", file=sys.stderr)
        return 2

    tribe = pathlib.Path(argv[1]).resolve()
    work = pathlib.Path(argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    riscv_home = os.environ.get("RISCV_HOME") or os.environ.get("RISCV") or "/home/me/riscv"
    os.environ["RISCV_HOME"] = riscv_home
    os.environ.setdefault("RISCV", riscv_home)
    os.environ["PATH"] = str(pathlib.Path(riscv_home) / "bin") + os.pathsep + os.environ.get("PATH", "")

    prefix = os.environ.get("RISCV_PREFIX", "riscv32-unknown-elf-")
    gcc = tool(prefix + "gcc")
    objcopy = tool(prefix + "objcopy")
    spike = tool("spike")

    missing = [name for name, path in ((prefix + "gcc", gcc), (prefix + "objcopy", objcopy), ("spike", spike)) if path is None]
    if missing:
        print("SKIP: missing external tool(s): " + ", ".join(missing))
        return SKIP

    backend = os.environ.get("TRIBE_SPIKE_FRAGMENTS_BACKEND", "cpphdl")
    if backend not in ("cpphdl", "verilator"):
        print(f"unsupported TRIBE_SPIKE_FRAGMENTS_BACKEND={backend!r}")
        return 2

    tribe_runner = tribe
    tribe_base_args: list[str] = ["--noveril"]
    if backend == "verilator":
        verilator_bin = pathlib.Path(
            os.environ.get("TRIBE_SPIKE_FRAGMENTS_VERILATOR_BIN", tribe.parent / "Tribe" / "obj_dir" / "VTribe")
        )
        if run([str(tribe), "1"], cwd=tribe.parent, check=False).returncode != 0:
            print("failed to build Verilator Tribe model")
            return 1
        if not verilator_bin.exists():
            print(f"Verilator Tribe binary not found: {verilator_bin}")
            return 1
        tribe_runner = verilator_bin
        tribe_base_args = []

    failed = []
    for fragment in FRAGMENTS:
        print(f"== {fragment.name} ==")
        if run_fragment(fragment, gcc, objcopy, spike, tribe_runner, tribe_base_args, work) != 0:
            failed.append(fragment.name)

    if failed:
        print("FAILED RV32 fragment(s): " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
