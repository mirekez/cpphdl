#!/usr/bin/env python3
"""Observe register-file traffic without recompiling the original Verilator model."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def decode_trace(data, cycles):
    if cycles < 1 or len(data) != 24 * cycles:
        raise ValueError("capture did not contain exactly the requested number of cycles")
    records = list(struct.iter_unpack("<QQHHBBH", data))
    if records[0][5] or any(row[2] > 1023 or row[3] > 1023 or row[4] > 3 or
                            row[5] > 1 or row[6] for row in records):
        raise ValueError("invalid register-file trace")
    return records


def main():
    fixtures = Path(__file__).with_name("automatic_request_tree")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", type=Path, required=True)
    parser.add_argument("--verilator-root", type=Path, required=True)
    parser.add_argument("--riscv", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--elf", type=Path, default=Path(__file__).with_name("matrix_multiply.riscv"))
    parser.add_argument("--cycles", type=int, default=46264)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("cycles must be positive")
    native = args.native_build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    includes = args.verilator_root.resolve() / "include"
    objects = [native / (name + ".o") for name in [
        "ariane_tb", "SimDTM", "SimJTAG", "msim_helper", "read_elf_dpi",
        "remote_bitbang", "verilated", "verilated_dpi", "verilated_threads", "verilated_vpi"]]
    objects.append(native / "Variane_testharness__ALL.a")
    dependencies = [*objects, native / "Variane_testharness",
                    native / "Variane_testharness.h", native / "Variane_testharness___024root.h",
                    fixtures / "CaptureRegfile.cc", fixtures / "RegfileTrace.h", args.elf.resolve()]
    manifest = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in dependencies}
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(("CPPHDL_", "NATIVE_TRACE_")) and name != "LD_PRELOAD"}
    environment.pop("CVA6_FIXED_CYCLES", None)
    trace = output / "matmul-regfile.bin"
    trace.unlink(missing_ok=True)
    environment["CPPHDL_REGFILE_TRACE"] = str(trace)
    commands = [
        [args.cxx, "-std=c++17", "-O2", "-I" + str(includes), "-I" + str(includes / "vltstd"),
         "-I" + str(native), "-I" + str(fixtures), "-c", fixtures / "CaptureRegfile.cc",
         "-o", output / "observer.o"],
        [args.cxx, output / "observer.o", *objects,
         "-Wl,--wrap=_ZN19Variane_testharness9eval_stepEv", "-L" + str(args.riscv.resolve() / "lib"),
         "-Wl,-rpath," + str(args.riscv.resolve() / "lib"), "-lfesvr", "-lriscv", "-ldisasm",
         "-pthread", "-o", output / "capture"],
        [output / "capture", "--seed=1", "--max-cycles=" + str(args.cycles),
         args.elf.resolve(), "+elf_file=" + str(args.elf.resolve())],
    ]
    for number, command in enumerate(commands):
        with (output / f"step-{number}.log").open("w") as log:
            subprocess.run(list(map(str, command)), cwd=output, env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    data = trace.read_bytes()
    if "PASSED" not in (output / "step-2.log").read_text():
        raise RuntimeError("original CVA6 matmul did not pass")
    records = decode_trace(data, args.cycles)
    if any(hashlib.sha256(path.read_bytes()).hexdigest() != manifest[str(path)] for path in dependencies):
        raise RuntimeError("original model or observer changed during capture")
    report = dict(cycles=len(records), reset_cycles=sum(not row[5] for row in records),
                  trace_sha256=hashlib.sha256(data).hexdigest(), source_sha256=manifest,
                  commands=[list(map(str, command)) for command in commands],
                  format="little-endian <QQHHBBH: wdata, read_value, raddr, waddr, we, reset_n, reserved")
    (output / "capture.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"captured {report['cycles']} cycles ({report['reset_cycles']} reset): {trace}")


if __name__ == "__main__":
    main()
