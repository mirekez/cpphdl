#!/usr/bin/env python3
"""Compare request-tree, arbiter, or register-file RTL through hdlcpp/cpphdl and Verilator."""

import argparse
import hashlib
import importlib.util
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
    parser.add_argument("--opt-level", choices=["O2", "O3"], default="O3")
    parser.add_argument("--inputs", type=int, nargs="+", default=[11, 16])
    parser.add_argument("--iterations", type=int, default=10000000)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--mode", choices=["optimize-combs", "optimize-combs-l1"])
    parser.add_argument("--replay-context", type=Path, help="full-design context exported by cpphdl --replay-export")
    parser.add_argument("--replay-source", help="original instance path, relative to the full root")
    parser.add_argument("--complete-arbiter", action="store_true",
                        help="exercise CVA6's complete 103-bit R arbiter, not just req_nodes")
    parser.add_argument("--cva6-metadata", type=Path,
                        help="reuse production conversion metadata from a regenerated CVA6 model")
    parser.add_argument("--regfile", action="store_true", help="exercise CVA6's complete integer register file")
    parser.add_argument("--regfile-trace", type=Path, help="replay captured matmul register-file inputs and check against full-CVA6 reads")
    args = parser.parse_args()
    if args.replay_context and (not args.mode or not args.replay_source):
        parser.error("--replay-context requires --mode and --replay-source")
    if args.replay_source and not args.replay_context:
        parser.error("--replay-source requires --replay-context")
    if any(count < 2 or count > 16 for count in args.inputs):
        parser.error("input counts must be between 2 and 16")
    if args.iterations < 1 or args.trials < 1:
        parser.error("iterations and trials must be positive")
    if args.complete_arbiter:
        args.inputs = [11]
    top = "ArbiterBench" if args.complete_arbiter else "RequestTreeBench"
    root_class = "ArbiterRoot" if args.complete_arbiter else "RequestTreeRoot"
    root_header = "ArbiterRoot.h" if args.complete_arbiter else "Root.h"
    seed = "ArbiterSeed.cc" if args.complete_arbiter else "Seed.cc"
    runner = "ArbiterRun.cc" if args.complete_arbiter else "Run.cc"
    native_runner = "ArbiterRun.cc" if args.complete_arbiter else "VerilatorRun.cc"
    if args.regfile:
        if args.complete_arbiter:
            parser.error("choose only one block")
        args.inputs = [2]
        top, root_class = "RegfileBench", "RegfileRoot"
        root_header, seed = "RegfileRoot.h", "RegfileSeed.cc"
        runner = native_runner = "RegfileRun.cc"
    if args.regfile_trace and not args.regfile:
        parser.error("--regfile-trace requires --regfile")
    trace_args = [args.regfile_trace.resolve()] if args.regfile_trace else []
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "results.json").unlink(missing_ok=True)
    cells = args.cva6_source.resolve() / "vendor/pulp-platform/common_cells"
    sources = [cells / "src" / (name + ".sv") for name in ["cf_math_pkg", "lzc", "rr_arb_tree"]]
    if args.regfile:
        sources = [args.cva6_source.resolve() / "core/include/config_pkg.sv",
                   args.cva6_source.resolve() / "core/ariane_regfile_ff.sv"]
    rtl = [*sources, fixtures / (top + ".sv")]
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(("HDLCPP_", "CPPHDL_")) and name != "LD_PRELOAD"}
    environment["HDLCPP_INCLUDE_DIRS"] = ":".join(map(str, [cells / "include", cells / "src", fixtures]))
    environment.pop("VERILATOR_ROOT", None)
    if args.cva6_metadata:
        if not (args.complete_arbiter or args.regfile):
            parser.error("--cva6-metadata requires --complete-arbiter or --regfile")
        for required in ["cva6_merged_port_types.tsv", "cva6_merged_module_params.tsv", "cva6_merged_module_traits.tsv"]:
            if not (args.cva6_metadata / required).is_file():
                parser.error("missing production metadata: " + required)
        for metadata in args.cva6_metadata.glob("*.tsv"):
            shutil.copy2(metadata, output / metadata.name)
        spec = importlib.util.spec_from_file_location(
            "cva6_conversion", Path(__file__).parent / "support/cpphdl/tools/convert_cva6.py")
        conversion = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(conversion)
        conversion.OUT = output
        conversion.setup_hdlcpp_env(environment, output / "cva6_merged_port_types.tsv",
                                    output / "cva6_merged_module_params.tsv")
    # Verilator's default -Os otherwise overrides -CFLAGS for its driver.
    optimization = "-" + args.opt_level
    environment["MAKEFLAGS"] = (environment.get("MAKEFLAGS", "") +
                                f" OPT_FAST={optimization} OPT_SLOW={optimization} OPT_GLOBAL={optimization} CXX={args.cxx}")
    manifest = {str(source): hashlib.sha256(source.read_bytes()).hexdigest() for source in rtl}
    if args.complete_arbiter and not args.cva6_metadata:
        bundle = fixtures / "ArbiterDesign.sv"
        manifest[str(bundle)] = hashlib.sha256(bundle.read_bytes()).hexdigest()
    (output / "rtl-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    tracked = [fixtures / name for name in {root_header, seed, runner, native_runner}]
    tracked += [*sorted((repo / "include").glob("*.h")), args.hdlcpp.resolve(), args.cpphdl.resolve()]
    if args.regfile:
        tracked.append(fixtures / "RegfileTrace.h")
    if args.regfile_trace:
        tracked.append(args.regfile_trace.resolve())
    if args.replay_context:
        tracked.append(args.replay_context.resolve())
    if args.cva6_metadata:
        tracked += list(args.cva6_metadata.glob("*.tsv"))
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
    (output / "inputs-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    (output / "environment.json").write_text(json.dumps(dict(
        compiler=args.cxx, optimization=optimization,
        settings={name: value for name, value in environment.items()
                  if name.startswith(("HDLCPP_", "CPPHDL_")) or name == "MAKEFLAGS"}), indent=2) + "\n")
    commands = []

    def run(command, directory, label):
        command = list(map(str, command))
        commands.append(dict(command=command, cwd=str(directory)))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        with (directory / (label + ".log")).open("w") as log:
            subprocess.run(command, cwd=directory, env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    # Convert actual RTL, with production conversion settings only when opted
    # in. Neither mode supplies a test-specific hardware implementation.
    if args.complete_arbiter and not args.cva6_metadata:
        run([args.hdlcpp.resolve(), fixtures / "ArbiterDesign.sv"], output, "convert-" + top)
    else:
        for source in sources:
            run([args.hdlcpp.resolve(), source], output, "convert-" + source.stem)
        if args.complete_arbiter:
            run([args.hdlcpp.resolve(), rtl[-1]], output, "convert-" + top)
    for fixture in [root_header, seed, runner]:
        shutil.copy2(fixtures / fixture, output / fixture)
    if args.regfile:
        shutil.copy2(fixtures / "RegfileTrace.h", output / "RegfileTrace.h")

    records = []
    summaries = []
    for count in args.inputs:
        build = output / str(count)
        build.mkdir(exist_ok=True)
        macro = "-DREQUEST_TREE_INPUTS=" + str(count)
        if args.cva6_metadata:
            macro += " -DCVA6_CONVERSION_METADATA"
        verilated = build / "verilator"
        run([args.verilator, "--cc", "--exe", "--build", "-j", "2", "-Wno-fatal",
             "--top-module", top, "--Mdir", verilated,
             *([] if args.regfile else ["-GINPUTS=" + str(count)]), "-I" + str(cells / "include"),
             "-CFLAGS", optimization + " -DNDEBUG -DUSE_VERILATOR " + macro,
             *rtl, fixtures / native_runner],
            build, "build-verilator")
        executables = {"verilator": verilated / ("V" + top)}
        for mode in ([args.mode] if args.mode else ["optimize-combs", "optimize-combs-l1"]):
            generated = build / mode
            generated.mkdir(exist_ok=True)
            for stale in generated.glob(root_class + "_optimized_combs*"):
                if stale.is_file():
                    stale.unlink()
            run([args.cpphdl.resolve(), "--" + mode, root_class,
                 *(["--replay-context=" + str(args.replay_context.resolve()),
                    "--replay-source=" + args.replay_source, "--replay-target=dut"] if args.replay_context else []),
                 "--generated-dir=" + str(generated), output / seed, "--",
                 "-std=c++23", "-w", *macro.split(), "-I" + str(repo / "include")],
                build, "generate-" + mode)
            emitted = sorted(generated.glob(root_class + "_optimized_combs*.cpp"))
            if not (args.complete_arbiter or args.regfile) and not any("sv_assign_bit(" in source.read_text() for source in emitted):
                raise RuntimeError("native-store lowering was not exercised")
            if args.regfile and not any(re.search(
                    r"__cpphdl_bit_target = n\d+\.we_dec_comb\[", source.read_text())
                    for source in emitted):
                raise RuntimeError("nested packed-store lowering was not exercised")
            executable = generated / "run"
            run([args.cxx, "-std=c++23", optimization, "-DNDEBUG", "-w", *macro.split(),
                 "-I" + str(repo / "include"), "-I" + str(output), "-I" + str(generated),
                 output / runner, *emitted, "-lstdc++exp", "-o", executable],
                build, "build-" + mode)
            executables[mode] = executable
        vectors = build / "verilator-vectors.bin"
        run([executables["verilator"], vectors, "1", *trace_args], build, "reference-vectors")
        for trial in range(1, args.trials + 1):
            variants = list(executables)
            if trial % 2 == 0:
                variants.reverse()
            checksums = set()
            for variant in variants:
                label = f"run-{variant}-{trial}"
                affinity = [] if args.cpu is None else ["taskset", "-c", str(args.cpu)]
                run([*affinity, executables[variant], vectors, args.iterations, *trace_args], build, label)
                result = (build / (label + ".log")).read_text()
                match = re.search(r"seconds=([0-9.]+) checksum=([0-9a-f]+)", result)
                if not match:
                    raise RuntimeError("missing benchmark result: " + label)
                checksums.add(match[2])
                if args.regfile or args.complete_arbiter:
                    cycle_match = re.search(r"cycles=(\d+)", result)
                    if not cycle_match or int(cycle_match[1]) != args.iterations:
                        raise RuntimeError("wrong simulated cycle count: " + label)
                record = dict(inputs=count, complete_arbiter=args.complete_arbiter, regfile=args.regfile,
                              iterations=args.iterations, variant=variant, trial=trial,
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
        summaries.append(dict(inputs=count, medians=medians,
                              relative_to_verilator={name: seconds / medians["verilator"]
                                                     for name, seconds in medians.items()}))
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest
           for path, digest in {**manifest, **hashes}.items()):
        raise RuntimeError("benchmark inputs changed during the test")
    (output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")


if __name__ == "__main__":
    main()
