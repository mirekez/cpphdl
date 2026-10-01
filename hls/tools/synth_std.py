#!/usr/bin/env python3
"""Measure shared-memory std container and RbMap RTL, without resizing it."""
import argparse
import hashlib
import json
import re
import resource
import shutil
import subprocess
import time
from pathlib import Path


CONTAINERS = ("Array", "Vector", "MapSmall", "Map", "List", "Multimap", "UnorderedMap")
FLOW = "xc7-shared-fast-opt-v2"


def script_path(path):
    # read_slang receives Yosys script quotes literally, unlike shell arguments.
    text = str(path)
    if re.search(r'[\s;"\\]', text):
        raise ValueError("Yosys script paths must not contain whitespace, quotes, backslashes or semicolons")
    return text


def report(output, rows, version, flow, storage):
    (output / "results.json").write_text(json.dumps({"yosys": version, "flow": flow, "storage": storage, "results": rows}, indent=2) + "\n")
    lines = ["# Container Synthesis", "", version, "",
             f"Shared-memory regression RTL, original pool sizes. Storage: {storage}. Xilinx xc7; no DSP,",
             "I/O pads or clock buffers. Classic ABC `strash; if` LUT6 mapping; no SAT sharing.",
             "Fast pre-mapping optimization avoids the expensive full mux-tree/reduction passes.",
             "LUTs count LUT1 through LUT6 (not carry cells, muxes, FFs or distributed RAM).",
             "These are synthesis counts, not placed/routed utilization or timing results.", "",
             "| Test | Arena bytes | LUTs | Flip-flops | CARRY4 | RAMB18 | RAMB36 | Distributed RAM cells | Seconds | Status |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    for row in rows:
        lines.append("| " + " | ".join(str(row.get(key, "-")) for key in
            ("name", "arena_bytes", "luts", "flip_flops", "carry4", "ramb18", "ramb36", "distributed_ram", "seconds", "status")) + " |")
    lines += ["", "Each subdirectory contains the exact Yosys script, input hashes, log and final statistics.",
              "The arena includes object/addressable storage as well as the allocation pool;",
              "it is not an element-count limit. MapSmall explicitly bounds distinct keys in C++.", ""]
    (output / "README.md").write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--storage", choices=("registers", "bram"), default="registers")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per container")
    parser.add_argument("--memory-mib", type=int, default=4096, help="address-space limit inherited by Yosys (0: unlimited)")
    parser.add_argument("--append", action="store_true", help="replace selected results while retaining other completed measurements")
    parser.add_argument("--containers", nargs="+", choices=(*CONTAINERS, "RbMap"), default=CONTAINERS)
    args = parser.parse_args()
    if args.timeout <= 0 or args.memory_mib < 0:
        parser.error("timeout must be positive and memory-mib must be nonnegative")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.memory_mib:
        limit = args.memory_mib * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    build, yosys = args.build.resolve(), args.yosys.resolve()
    flow = FLOW + ("-bram-v3" if args.storage == "bram" else "")
    output = (args.output or build / ("hls/std-synthesis-bram" if args.storage == "bram" else "hls/std-synthesis")).resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([str(yosys), "-V"], text=True).strip()
    rows = []
    if args.append and (output / "results.json").is_file():
        previous = json.loads((output / "results.json").read_text())
        if previous["yosys"] != version or previous.get("flow") != flow:
            parser.error("cannot append measurements from a different Yosys version or synthesis flow")
        rows = previous["results"]
    for name in args.containers:
        work = output / name
        work.mkdir(exist_ok=True)
        variant = "_bram" if args.storage == "bram" else ""
        directory = "hls/examples/map" if name == "RbMap" else "hls/tests/std"
        generated = build / directory / f"hls_clocked_{name}_shared_memory{variant}-rtl/generated"
        top = f"Clocked{name}Top"
        files = [generated / "Predef_pkg.sv", *sorted(generated.glob("*Methods*.sv")), generated / (top + ".sv")]
        row = {"name": name, "status": "missing RTL", "memory_limit_mib": args.memory_mib,
               "timeout_seconds": args.timeout}
        existing = next((i for i, previous in enumerate(rows) if previous["name"] == name), None)
        if existing is None:
            rows.append(row)
        else:
            rows[existing] = row
        if len(files) != 3 or any(not path.is_file() for path in files):
            report(output, rows, version, flow, args.storage)
            continue
        text = files[1].read_text()
        arena = re.search(r"localparam(?:\s+\w+)*\s+MEM_BYTES\s*=\s*(\d+)", text)
        row["arena_bytes"] = int(arena.group(1)) if arena else "unknown"
        for parameter, key in (("ADDR_BITS", "address_bits"), ("HEAP_BYTES", "heap_bytes")):
            match = re.search(r"localparam(?:\s+\w+)*\s+" + parameter + r"\s*=\s*(\d+)", text)
            row[key] = int(match.group(1)) if match else "unknown"
        (work / "inputs.json").write_text(json.dumps({str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                                     for path in files}, indent=2) + "\n")
        snapshot = work / "rtl"
        snapshot.mkdir(exist_ok=True)
        for path in files:
            shutil.copy2(path, snapshot / path.name)
        files = [snapshot / path.name for path in files]
        ram_option = "" if args.storage == "bram" else "-nobram"
        ram_inference = "memory_dff; opt_clean" if args.storage == "bram" else ""
        ram_flatten = "flatten -noscopeinfo; opt_clean" if args.storage == "bram" else ""
        hierarchy_option = "--keep-hierarchy" if args.storage == "bram" else ""
        script = f"""read_slang --no-proc {hierarchy_option} --top {top} {' '.join(map(script_path, files))}
proc_clean; proc_rmdead; proc_prune; proc_init; proc_rom; proc_mux; proc_clean
opt_expr -keepdc; opt_clean -purge; opt -fast; wreduce; opt -fast
{ram_inference}
check -assert
synth_xilinx -top {top} -family xc7 -flatten -noiopad -noclkbuf -nodsp {ram_option} -run begin:prepare
techmap -map +/cmp2lut.v -map +/cmp2lcu.v -D LUT_WIDTH=6
alumacc; opt -fast; memory -nomap; opt_clean
synth_xilinx -top {top} -family xc7 -noiopad -noclkbuf -nodsp {ram_option} -run map_memory:fine
{ram_flatten}
opt -fast
xilinx_srl -variable -minlen 3
techmap -map +/techmap.v -D LUT_SIZE=6 -map +/xilinx/arith_map.v
opt -fast
synth_xilinx -top {top} -family xc7 -noiopad -noclkbuf -nodsp {ram_option} -run map_cells:map_luts
abc -lut 6 -script "+strash; if"
techmap -map +/xilinx/lut_map.v -D LUT_WIDTH=6
opt_lut_ins -tech xilinx; clean; blackbox =A:whitebox; check -assert
tee -o {script_path(work / 'stat.json')} stat -json
"""
        (work / "synth.ys").write_text(script)
        (work / "stat.json").unlink(missing_ok=True)
        print(f"Synthesizing {name}: {row['arena_bytes']} arena bytes", flush=True)
        started = time.monotonic()
        with (work / "yosys.log").open("w") as log:
            result = subprocess.run(["timeout", "--kill-after=10", str(args.timeout), str(yosys),
                                     "-Q", "-T", "-s", str(work / "synth.ys")], stdout=log, stderr=subprocess.STDOUT)
        row["seconds"] = round(time.monotonic() - started, 1)
        row["status"] = "passed" if result.returncode == 0 else f"failed (exit {result.returncode})"
        if result.returncode == 124:
            row["status"] = "timeout"
        elif result.returncode != 0:
            with (work / "yosys.log").open() as log:
                if any("std::bad_alloc" in line for line in log):
                    row["status"] = "allocation failed"
        if result.returncode == 0:
            stats = json.loads((work / "stat.json").read_text())
            cells = stats["modules"]["\\" + top]["num_cells_by_type"]
            row["cells"] = cells
            row["luts"] = sum(cells.get(f"LUT{i}", 0) for i in range(1, 7))
            row["flip_flops"] = sum(count for kind, count in cells.items() if kind.startswith("FD"))
            row["carry4"] = cells.get("CARRY4", 0)
            row["ram_cells"] = sum(count for kind, count in cells.items() if kind.startswith("RAM"))
            row["ramb18"] = sum(count for kind, count in cells.items() if kind.startswith("RAMB18"))
            row["ramb36"] = sum(count for kind, count in cells.items() if kind.startswith("RAMB36"))
            row["distributed_ram"] = sum(count for kind, count in cells.items() if kind.startswith("RAM") and not kind.startswith("RAMB"))
            if stats["modules"]["\\" + top].get("num_submodules", 0):
                row["status"] = "incomplete flattening"
            elif any(kind.startswith("$") for kind in cells):
                row["status"] = "incomplete mapping"
            elif args.storage == "bram" and row["ramb18"] + row["ramb36"] == 0:
                row["status"] = "BRAM not inferred"
        report(output, rows, version, flow, args.storage)
        print(f"{name}: {row['status']}, LUTs={row.get('luts', '-')}, {row['seconds']}s", flush=True)
    return int(any(row["status"] != "passed" for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
