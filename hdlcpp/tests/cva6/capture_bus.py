#!/usr/bin/env python3
"""Capture settled CVA6 crossbar boundary traffic, including its local reset."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def instrument_requests(source):
    name = "ariane_testharness__DOT__i_axi_xbar__DOT__slv_reqs"
    declaration = f"VlWide<24>/*747:0*/ {name};"
    if source.count(declaration) != 1:
        raise ValueError("native build does not have the expected 748-bit bus boundary")
    start = source.rfind("\nvoid ", 0, source.index(declaration))
    end = source.find("\n}\n", source.index(declaration))
    if start < 0 or end < 0:
        raise ValueError("cannot locate native boundary producer")
    return ('extern "C" void cpphdl_observe_bus_requests(const unsigned*);\n' +
            source[:end] + f"\n    cpphdl_observe_bus_requests({name}.data());" + source[end:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", type=Path, required=True)
    parser.add_argument("--verilator-root", type=Path, required=True)
    parser.add_argument("--riscv", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--elf", type=Path, default=Path(__file__).with_name("matrix_multiply.riscv"))
    parser.add_argument("--cycles", type=int, default=46264)
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("cycles must be positive")
    native, output = args.native_build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).with_name("context_replay")
    original = native / "Variane_testharness___024root__1.cpp"
    observed = output / original.name
    observed.write_text(instrument_requests(original.read_text()))
    includes = args.verilator_root.resolve() / "include"
    objects = [native / (name + ".o") for name in ["ariane_tb", "SimDTM", "SimJTAG", "msim_helper",
               "read_elf_dpi", "remote_bitbang", "verilated", "verilated_dpi", "verilated_threads", "verilated_vpi"]]
    objects.append(native / "Variane_testharness__ALL.a")
    flags = ["g++", "-std=c++17", "-O2", "-I" + str(native), "-I" + str(includes),
             "-I" + str(includes / "vltstd"), "-I" + str(fixtures)]
    commands = [
        [*flags, "-c", observed, "-o", output / "producer.o"],
        [*flags, "-c", fixtures / "CaptureBus.cc", "-o", output / "observer.o"],
        ["g++", output / "producer.o", output / "observer.o", *objects,
         "-Wl,--wrap=_ZN19Variane_testharness9eval_stepEv", "-L" + str(args.riscv.resolve() / "lib"),
         "-Wl,-rpath," + str(args.riscv.resolve() / "lib"), "-lfesvr", "-lriscv", "-ldisasm", "-pthread",
         "-o", output / "capture"],
        [output / "capture", "--seed=1", "--max-cycles=" + str(args.cycles),
         args.elf.resolve(), "+elf_file=" + str(args.elf.resolve())],
    ]
    dependencies = [original, *objects, native / "Variane_testharness", args.elf.resolve(),
                    fixtures / "CaptureBus.cc", fixtures / "BusTrace.h", Path(__file__).resolve()]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in dependencies}
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(("CPPHDL_", "NATIVE_TRACE_"))
                   and name not in ("LD_PRELOAD", "CVA6_FIXED_CYCLES")}
    environment["CPPHDL_BUS_TRACE"] = str(output / "matmul-bus.bin")
    for number, command in enumerate(commands):
        with (output / f"step-{number}.log").open("w") as log:
            subprocess.run(list(map(str, command)), cwd=output, env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    log = (output / "step-3.log").read_text()
    if "PASSED" not in log or (output / "matmul-bus.bin").stat().st_size != 800 * args.cycles:
        raise RuntimeError("capture did not pass matmul with the requested cycle count")
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in hashes.items()):
        raise RuntimeError("capture inputs changed while running")
    report = dict(cycles=args.cycles, source_sha256=hashes,
                  trace_sha256=hashlib.sha256((output / "matmul-bus.bin").read_bytes()).hexdigest(),
                  commands=[list(map(str, command)) for command in commands])
    (output / "capture.json").write_text(json.dumps(report, indent=2) + "\n")
    print("captured", args.cycles, "bus cycles")


if __name__ == "__main__":
    main()
