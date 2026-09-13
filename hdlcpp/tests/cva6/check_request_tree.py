#!/usr/bin/env python3
"""Differentially test untouched CVA6 RTL through hdlcpp and cpphdl."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess


def main():
    repo = Path(__file__).resolve().parents[3]
    fixtures = Path(__file__).with_name("automatic_request_tree")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cva6-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hdlcpp", type=Path, default=repo / "hdlcpp/build/hdlcpp")
    parser.add_argument("--cpphdl", type=Path, default=repo / "build/cpphdl")
    parser.add_argument("--verilator", default="verilator")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--inputs", type=int, nargs="+", default=[11, 16])
    parser.add_argument("--iterations", type=int, default=10000000)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if any(count < 2 or count > 16 for count in args.inputs):
        parser.error("input counts must be between 2 and 16")
    if args.iterations < 1 or args.trials < 1:
        parser.error("iterations and trials must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "results.json").unlink(missing_ok=True)
    cells = args.cva6_source.resolve() / "vendor/pulp-platform/common_cells"
    sources = [cells / "src" / (name + ".sv") for name in ["cf_math_pkg", "lzc", "rr_arb_tree"]]
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(("HDLCPP_", "CPPHDL_")) and name != "LD_PRELOAD"}
    environment["HDLCPP_INCLUDE_DIRS"] = str(cells / "include")
    environment.pop("VERILATOR_ROOT", None)
    manifest = {str(source): hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}
    (output / "rtl-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    commands = []

    def run(command, directory, label):
        command = list(map(str, command))
        commands.append(dict(command=command, cwd=str(directory)))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        with (directory / (label + ".log")).open("w") as log:
            subprocess.run(command, cwd=directory, env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    # Convert the actual source files. No copied method bodies, line-patch maps,
    # specialized Boolean implementations, or generated-file edits enter here.
    for source in sources:
        run([args.hdlcpp.resolve(), source], output, "convert-" + source.stem)
    for fixture in ["Root.h", "Seed.cc", "Run.cc"]:
        shutil.copy2(fixtures / fixture, output / fixture)

    records = []
    for count in args.inputs:
        build = output / str(count)
        build.mkdir(exist_ok=True)
        macro = "-DREQUEST_TREE_INPUTS=" + str(count)
        verilated = build / "verilator"
        run([args.verilator, "--cc", "--exe", "--build", "-j", "2", "-Wno-fatal",
             "--top-module", "RequestTreeBench", "--Mdir", verilated,
             "-GINPUTS=" + str(count), "-I" + str(cells / "include"),
             "-CFLAGS", "-O3 -DNDEBUG " + macro,
             *sources, fixtures / "RequestTreeBench.sv", fixtures / "VerilatorRun.cc"],
            build, "build-verilator")
        executables = {"verilator": verilated / "VRequestTreeBench"}
        for mode in ["optimize-combs", "optimize-combs-l1"]:
            generated = build / mode
            generated.mkdir(exist_ok=True)
            for stale in generated.glob("RequestTreeRoot_optimized_combs*"):
                if stale.is_file():
                    stale.unlink()
            run([args.cpphdl.resolve(), "--" + mode, "RequestTreeRoot",
                 "--generated-dir=" + str(generated), output / "Seed.cc", "--",
                 "-std=c++23", "-w", macro, "-I" + str(repo / "include")],
                build, "generate-" + mode)
            emitted = sorted(generated.glob("RequestTreeRoot_optimized_combs*.cpp"))
            if not any("sv_assign_bit(" in source.read_text() for source in emitted):
                raise RuntimeError("native-store lowering was not exercised")
            executable = generated / "run"
            run([args.cxx, "-std=c++23", "-O3", "-DNDEBUG", "-w", macro,
                 "-I" + str(repo / "include"), "-I" + str(output), "-I" + str(generated),
                 output / "Run.cc", *emitted, "-lstdc++exp", "-o", executable],
                build, "build-" + mode)
            executables[mode] = executable
        vectors = build / "verilator-vectors.bin"
        run([executables["verilator"], vectors, "1"], build, "reference-vectors")
        for trial in range(1, args.trials + 1):
            variants = list(executables)
            if trial % 2 == 0:
                variants.reverse()
            checksums = set()
            for variant in variants:
                label = f"run-{variant}-{trial}"
                affinity = [] if args.cpu is None else ["taskset", "-c", str(args.cpu)]
                run([*affinity, executables[variant], vectors, args.iterations], build, label)
                result = (build / (label + ".log")).read_text()
                match = re.search(r"seconds=([0-9.]+) checksum=([0-9a-f]+)", result)
                if not match:
                    raise RuntimeError("missing benchmark result: " + label)
                checksums.add(match[2])
                record = dict(inputs=count, variant=variant, trial=trial,
                              seconds=float(match[1]), checksum=match[2])
                records.append(record)
                print(json.dumps(record), flush=True)
                (output / "results.json").write_text(json.dumps(records, indent=2) + "\n")
            if len(checksums) != 1:
                raise RuntimeError("timed output checksums disagree")
        medians = {variant: statistics.median(record["seconds"] for record in records
                                             if record["inputs"] == count and record["variant"] == variant)
                   for variant in executables}
        print("medians", count, json.dumps(medians), flush=True)
    if any(hashlib.sha256(source.read_bytes()).hexdigest() != manifest[str(source)] for source in sources):
        raise RuntimeError("RTL changed during the test")


if __name__ == "__main__":
    main()
