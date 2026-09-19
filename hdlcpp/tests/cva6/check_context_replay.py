#!/usr/bin/env python3
"""Regenerate and check a production-context AXI crossbar matmul replay."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_manifest(path):
    for filename, digest in json.loads(path.read_text()).items():
        if file_hash(filename) != digest:
            raise RuntimeError("replay input or executable changed: " + filename)


def native_layout_flags(enabled):
    return ["-DCPPHDL_NATIVE_PACKED"] if enabled else []


def main():
    repo = Path(__file__).resolve().parents[3]
    fixtures = Path(__file__).with_name("context_replay")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cva6-source", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--context", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hdlcpp", type=Path, default=repo / "hdlcpp/build/hdlcpp")
    parser.add_argument("--cpphdl", type=Path, default=repo / "build/cpphdl")
    parser.add_argument("--verilator", required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--reuse-build", action="store_true")
    parser.add_argument("--native-packed", action="store_true",
                        help="store compiler-proved packed aggregates as native fields (C++ ABI change)")
    args = parser.parse_args()
    if args.trials < 1 or args.repeats < 1: parser.error("trials/repeats must be positive")
    source, model, output = args.cva6_source.resolve(), args.model.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    generated, optimized = output / "generated", output / "l1"
    generated.mkdir(exist_ok=True)
    optimized.mkdir(exist_ok=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("HDLCPP_", "CPPHDL_")) and key not in ("LD_PRELOAD", "VERILATOR_ROOT")}
    environment["MAKEFLAGS"] = f"OPT_FAST=-O2 OPT_SLOW=-O2 OPT_GLOBAL=-O2 CXX={args.cxx}"
    commands_path = output / "commands.json"
    commands = json.loads(commands_path.read_text()) if args.reuse_build else []
    layout_manifest = output / "native-packed.json"
    if args.reuse_build:
        previous_layout = json.loads(layout_manifest.read_text()) if layout_manifest.exists() else False
        if previous_layout != args.native_packed:
            raise RuntimeError("native packed layout differs from the saved build")
    else:
        layout_manifest.write_text(json.dumps(args.native_packed) + "\n")
    layout_flags = native_layout_flags(args.native_packed)

    def run(command, label, cwd=output):
        command = list(map(str, command))
        commands.append(dict(command=command, cwd=str(cwd)))
        commands_path.write_text(json.dumps(commands, indent=2) + "\n")
        with (output / (label + ".log")).open("w") as log:
            subprocess.run(command, cwd=cwd, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)

    if not args.reuse_build:
        for path in model.glob("*.tsv"): shutil.copy2(path, output / path.name)
        spec = importlib.util.spec_from_file_location("conversion", Path(__file__).parent / "support/cpphdl/tools/convert_cva6.py")
        conversion = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(conversion)
        conversion.OUT = output
        conversion.setup_hdlcpp_env(environment, output / "cva6_merged_port_types.tsv", output / "cva6_merged_module_params.tsv")
        includes = [source / "vendor/pulp-platform/axi/include", source / "vendor/pulp-platform/common_cells/include",
                    source / "vendor/pulp-platform/axi/src", source / "vendor/pulp-platform/common_cells/src",
                    source / "corev_apu/tb", fixtures, output]
        environment["HDLCPP_INCLUDE_DIRS"] = ":".join(map(str, includes))
        environment["HDLCPP_DEFINES"] = "VERILATOR"
        for name in ["XbarRoot.h", "XbarSeed.cc", "XbarElaborate.cc", "XbarBench.sv", "XbarDesign.sv", "BusRun.cc", "BusTrace.h"]:
            shutil.copy2(fixtures / name, output / name)
        harness = (source / "corev_apu/tb/ariane_testharness.sv").read_text()
        mapping = re.search(r"assign addr_map\s*=\s*'\{.*?\n  \};", harness, re.DOTALL)
        if not mapping: raise RuntimeError("cannot extract the original testharness address map")
        (output / "AddressMap.svh").write_text(mapping[0].replace("assign addr_map", "assign rules") + "\n")
        # Constant-index wiring avoids the converter's interface-array generate
        # alias limitation; the actual crossbar and AXI macros remain original RTL.
        wiring = []
        for port in range(2):
            wiring.extend([f"`AXI_ASSIGN_FROM_REQ(slave[{port}], slv_reqs[{port}])",
                           f"`AXI_ASSIGN_TO_RESP(slv_resps[{port}], slave[{port}])"])
        for port in range(10):
            wiring.extend([f"`AXI_ASSIGN_TO_REQ(mst_reqs[{port}], master[{port}])",
                           f"`AXI_ASSIGN_FROM_RESP(master[{port}], mst_resps[{port}])"])
        (output / "BoundaryWiring.svh").write_text("\n".join(wiring) + "\n")
        names = ["axi_pkg", "cf_math_pkg", "lzc", "rr_arb_tree", "delta_counter", "counter", "spill_register_flushable",
                 "spill_register", "fifo_v3", "fifo_v2", "addr_decode", "stream_register", "axi_id_prepend",
                 "axi_atop_filter", "axi_err_slv", "axi_mux", "axi_demux", "axi_xbar", "axi_intf", "ariane_soc_pkg"]
        inputs = [*fixtures.iterdir(), args.trace.resolve(), args.context.resolve(), args.cpphdl.resolve(), args.hdlcpp.resolve(),
                  Path(__file__).resolve(), *model.glob("*.tsv"),
                  source / "corev_apu/tb/ariane_testharness.sv", *sorted((repo / "include").glob("*.h")),
                  *source.glob("vendor/pulp-platform/*/include/**/*.svh")]
        rtl_sources = []
        for name in names:
            headers = list((model / "generated").rglob(name + ".h"))
            if len(headers) != 1: raise RuntimeError("ambiguous production module " + name)
            rtl = source / headers[0].relative_to(model / "generated").with_suffix(".sv")
            inputs.append(rtl)
            rtl_sources.append((name, rtl))
        hashes = {str(path): file_hash(path) for path in inputs if path.is_file()}
        (output / "inputs-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
        for name, rtl in rtl_sources:
            run([args.hdlcpp.resolve(), rtl], "convert-" + name)
            converted = generated / (name + ".h")
            if not converted.exists(): raise RuntimeError("missing converted header " + str(converted))
        run([args.hdlcpp.resolve(), output / "XbarBench.sv"], "convert-bench")
        run([args.hdlcpp.resolve(), "--optimize", output / "XbarElaborate.cc"], "elaborate")
        aliases = [line for line in (output / "cpphdl_optimized_externs.h").read_text().splitlines() if line.startswith("using cpphdl_opt_t")]
        (output / "XbarAliases.h").write_text("#pragma once\n" + "\n".join(aliases) + "\n")
        run([args.cpphdl.resolve(), "--optimize-combs-l1", "XbarRoot", "--generated-dir=" + str(optimized),
             "--replay-context=" + str(args.context.resolve()), "--replay-source=i_axi_xbar", "--replay-target=dut.dut",
             "--replay-export=" + str(output / "replay-context.txt"), output / "XbarSeed.cc", "--", "-std=c++23", "-w",
             "-I" + str(repo / "include"), *layout_flags], "optimize")
        native = output / "verilator"
        # Match the production Verilator frontend flags, separately from C++ -O2.
        run([args.verilator, "-O3", "--unroll-count", "256", "--vpi", "--threads-dpi", "none", "--no-timing",
             "--cc", "--exe", "--build", "-j", "2", "-Wno-fatal", "--top-module", "XbarBench",
             "--Mdir", native, *["-I" + str(path) for path in includes], "-CFLAGS", "-O2 -DNDEBUG -DUSE_VERILATOR",
             output / "XbarDesign.sv", output / "BusRun.cc"], "build-native")
        objects = output / "objects"
        objects.mkdir(exist_ok=True)
        flags = [args.cxx, "-std=c++23", "-O2", "-DNDEBUG", "-w", "-I" + str(repo / "include"),
                 "-I" + str(output), "-I" + str(optimized), *layout_flags]
        sources = [output / "BusRun.cc", *sorted(optimized.glob("*.cpp"))]
        commands.extend(dict(command=[*flags, "-c", str(path), "-o", str(objects / (path.stem + ".o"))],
                             cwd=str(output)) for path in sources)
        commands_path.write_text(json.dumps(commands, indent=2) + "\n")
        def compile_source(path):
            target = objects / (path.stem + ".o")
            command = [*flags, "-c", path, "-o", target]
            with (objects / (path.stem + ".log")).open("w") as log:
                subprocess.run(list(map(str, command)), cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
            return target
        with ThreadPoolExecutor(max_workers=2) as pool: built = list(pool.map(compile_source, sources))
        run([args.cxx, *built, "-lstdc++exp", "-o", output / "run"], "link")
    binaries = {"verilator": output / "verilator/VXbarBench", "cpphdl": output / "run"}
    verify_manifest(output / "inputs-sha256.json")
    binary_manifest = output / "binaries-sha256.json"
    if args.reuse_build:
        verify_manifest(binary_manifest)
    else:
        binary_manifest.write_text(json.dumps({str(path): file_hash(path) for path in binaries.values()}, indent=2) + "\n")
    records = []
    for trial in range(args.trials):
        order = list(binaries)
        if trial % 2: order.reverse()
        for variant in order:
            label = f"timing-{trial}-{variant}"
            run(["taskset", "-c", args.cpu, binaries[variant], args.trace.resolve(), args.repeats], label)
            fields = dict(re.findall(r"(\w+)=([^\s]+)", (output / (label + ".log")).read_text()))
            records.append(dict(trial=trial, variant=variant, **fields))
    if len({(row["cycles"], row["checked"], row["checksum"]) for row in records}) != 1:
        raise RuntimeError("cycle/checksum mismatch")
    verify_manifest(output / "inputs-sha256.json")
    verify_manifest(binary_manifest)
    (output / "timings.json").write_text(json.dumps(records, indent=2) + "\n")
    summary = {variant: statistics.median(float(row["seconds"]) for row in records if row["variant"] == variant) for variant in binaries}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(summary)


if __name__ == "__main__":
    main()
