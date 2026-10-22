#!/usr/bin/env python3
"""Convert the actual C++ hardware class and run its contract against generated RTL."""
import argparse
import filecmp
import hashlib
from collections import Counter
import json
import os
import re
import shlex
import sys
import tempfile
from pathlib import Path
import subprocess


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import configure_cleanup, execute


def source_dependencies(roots, repo, cxx='c++', defines=()):
    """Use the real preprocessor: inactive includes are not DUT dependencies."""
    repo, found = repo.resolve(), set()
    for path in roots:
        with tempfile.TemporaryFile(mode='w+t') as dependencies:
            execute([str(cxx), '-std=c++17', '-MM', '-MT', 'cpphdl_deps', '-I' + str(repo / 'include'),
                     *defines, str(path.resolve())], stdout=dependencies, check=True, timeout=30)
            dependencies.seek(0)
            listing = dependencies.read().replace('\\\n', ' ').split(':', 1)[1]
        for name in shlex.split(listing):
            child = Path(name.replace('$$', '$')).resolve()
            if child.is_relative_to(repo):
                found.add(child)
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(found)}


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / "hardware_checks.json").unlink(missing_ok=True)
    source = Path(__file__).resolve().parents[1]
    repo = source.parents[1]
    filter_case = args.case.startswith('filter')
    clocks = (["--primary_clock", "ref_clk", "125000000", "--secondary_clock", "rx_clk", "125000000"]
              if args.case == "alignment" else ["--primary_clock", "clk", "250000000"]
              if args.case == "adc_sar" else ["--primary_clock", "clk", "125000000"]
              if args.case == "autoneg_link" else [])
    module = {"alignment": "ReceiveClockAligner", "pcs": "PCSSymbolMapper", "scrambler": "PCSSideStream",
              "transmit_bits": "PCSTransmitBits", "transmitter": "PCSTransmitter",
              "branch_metrics": "PCSBranchMetrics", "branch_metrics_pipeline": "PipelinedPCSBranchMetrics",
              "trellis": "PCSTrellisDecoder",
              "synchronize": "PCSReceiveSynchronizer", "receiver": "PCSReceiver", "framer": "PCSReceiveFramer",
              "filter": "ReceiveAdaptiveFilter", "filter_pipeline": "PipelinedReceiveFilter",
              "filter_datapath": "ReceiveFilter",
              "timing": "ReceiveTimingLoop", "adc_sar": "ADCSARController", "flp_tx": "FLPTransmitter",
              "flp_rx": "FLPReceiver", "master_slave": "MasterSlaveResolver",
              "negotiation": "Autonegotiator", "autoneg_link": "Autonegotiation"}[args.case]
    implementation = ("autonegotiation.cpp" if args.case == "autoneg_link" else
                      "negotiation.cpp" if args.case == "negotiation" else
                      "master_slave.cpp" if args.case == "master_slave" else
                      "flp_receive.cpp" if args.case == "flp_rx" else
                      "flp_transmit.cpp" if args.case == "flp_tx" else
                      "adc_sar_control.cpp" if args.case == "adc_sar" else
                      "receive_alignment.cpp" if args.case == "alignment" else
                      "pcs_receiver.cpp" if args.case == "receiver" else
                      "pcs_synchronize.cpp" if args.case == "synchronize" else
                      "pcs_framer.cpp" if args.case == "framer" else
                      "receive_timing.cpp" if args.case == "timing" else
                      "receive_filter_datapath.cpp" if args.case == "filter_datapath" else
                      "receive_filter.cpp" if args.case == "filter" else
                      "receive_filter_pipeline.cpp" if args.case == "filter_pipeline" else
                      "pcs_branch_metrics_pipeline.cpp" if args.case == "branch_metrics_pipeline" else
                      "pcs_receive.cpp" if args.case in ("branch_metrics", "trellis") else
                      "pcs_transmit.cpp" if args.case.startswith("transmit") else "pcs.cpp")
    harness_name = {"filter_pipeline": "filter.cpp", "branch_metrics_pipeline": "branch_metrics.cpp"}.get(
        args.case, args.case + ".cpp")
    defines = {"filter_pipeline": " -DPIPELINED_FILTER",
               "branch_metrics_pipeline": " -DPIPELINED_METRICS"}.get(args.case, "")
    source_hashes = source_dependencies([source / implementation, source / "tests" / harness_name], repo,
                                        args.cxx, shlex.split(defines))
    fixture_args = [source / "tests"] if args.case in ("pcs", "branch_metrics", "branch_metrics_pipeline", "trellis") else []
    if args.case.startswith("transmit"):
        fixture_args = [work / "transmit-vectors.txt"]
        execute(["python3", str(source / "tests/transmit_reference.py"), str(fixture_args[0])],
                       check=True, timeout=60)
    if args.case == "synchronize":
        fixture_args = [work / "synchronize-vectors.txt"]
        execute(["python3", str(source / "tests/synchronize_reference.py"), str(fixture_args[0])],
                       check=True, timeout=60)
    if args.case == "framer":
        fixture_args = [work / "framer-vectors.txt"]
        execute(["python3", str(source / "tests/framer_reference.py"), str(fixture_args[0])],
                       check=True, timeout=60)
    if args.case == "receiver":
        fixture_args = [work / "receiver-vectors.txt"]
        execute(["python3", str(source / "tests/receiver_reference.py"), str(fixture_args[0])],
                       check=True, timeout=60)
        execute(["python3", str(source / "tests/receiver_faults_reference.py"),
                        str(work / "receiver-fault-vectors.txt")], check=True, timeout=60)
    if filter_case:
        fixture_args = [work / "filter-vectors.txt"]
        generator = "filter_reference.py" if args.case == "filter" else "filter_pipeline_reference.py"
        execute(["python3", str(source / "tests" / generator), str(fixture_args[0])],
                       check=True, timeout=60)
    if args.case == "timing":
        fixture_args = [work / "timing-vectors.txt"]
        execute(["python3", str(source / "tests/timing_reference.py"), str(fixture_args[0])],
                check=True, timeout=60)
    harness = source / "tests" / harness_name
    fixture_paths = [path for argument in fixture_args for path in
                     (sorted(Path(argument).glob('*.csv')) if Path(argument).is_dir() else [Path(argument)])]
    if args.case == "receiver":
        fixture_paths.append(work / "receiver-fault-vectors.txt")
    fixture_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in fixture_paths}
    rtl_files = [work / "rtl/Predef_pkg.sv"]
    rtl_files += [work / "rtl" / (name + ".sv") for name in
                 (["PCSReceiveSynchronizer", "PCSReceiveFramer", "PipelinedPCSBranchMetrics", "PCSTrellisDecoder", "PCSReceiver"]
                  if args.case == "receiver" else ["PCSSymbolMapper", "PCSTransmitBits", "PCSTransmitter"]
                  if args.case == "transmitter" else ["PipelinedReceiveFilter", "ReceiveFilter"]
                  if args.case == "filter_datapath" else ["MasterSlaveResolver", "Autonegotiator"]
                  if args.case == "negotiation" else ["ReferenceMillisecond", "FLPTransmitter", "FLPReceiver",
                                                        "MasterSlaveResolver", "Autonegotiator", "Autonegotiation"]
                  if args.case == "autoneg_link" else [module])]
    stage_timeout = 1200 if filter_case or args.case == "autoneg_link" else 600 if args.case in ("trellis", "receiver", "branch_metrics_pipeline") else 180
    commands = [
        [args.native, *fixture_args, work / "cpp_trace.csv"],
        [args.converter, *clocks, "--generated-dir", work / "rtl", source / implementation, "--", "-I" + str(repo / "include")],
        [args.verilator, "--cc", "--exe", "--build", "--no-skip-identical", "-j", "2", "--top-module", module, "-Wno-fatal",
         "--Mdir", work / "obj", "-CFLAGS", "-DVERILATOR -std=c++17" + defines, *rtl_files,
         harness,
         "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx],
        [work / "obj" / ("V" + module), *fixture_args, work / "rtl_trace.csv"]]
    for index, command in enumerate(commands):
        with (work / f"stage{index}.log").open("w") as log:
            result = execute([str(value) for value in command], stdout=log, stderr=subprocess.STDOUT, timeout=stage_timeout)
        if result.returncode:
            raise RuntimeError((work / f"stage{index}.log").read_text())
    if args.case == "alignment":
        rtl = (work / "rtl/ReceiveClockAligner.sv").read_text()
        for clock in ("rx_clk", "ref_clk"):
            if f"always_ff @(posedge {clock})" not in rtl:
                raise ValueError("converter lost clock domain " + clock)
        if rtl.count('(* ASYNC_REG = "TRUE" *)') != 8:
            raise ValueError("converter lost pointer/fault synchronizer attributes")
    if args.case == "flp_rx":
        rtl = (work / 'rtl/FLPReceiver.sv').read_text()
        if rtl.count('(* ASYNC_REG = "TRUE" *)') != 2:
            raise ValueError('converter lost FLP input synchronizer attributes')
    if not filecmp.cmp(work / "cpp_trace.csv", work / "rtl_trace.csv", shallow=False):
        raise ValueError("native/generated RTL output trajectories differ")
    print((work / "stage3.log").read_text())
    print("Native and RTL trajectories match exactly.")
    def check_receiver_faults(binary, flow):
        command = [str(binary), "--faults", str(work / "receiver-fault-vectors.txt"), str(work / (flow + "_fault_trace.csv"))]
        execute(command, check=True, timeout=60)
        if flow != "cpp" and not filecmp.cmp(work / "cpp_fault_trace.csv", work / (flow + "_fault_trace.csv"), shallow=False):
            raise ValueError("native/" + flow + " receiver fault trajectories differ")
    if args.case == "receiver":
        check_receiver_faults(args.native, "cpp")
        check_receiver_faults(work / "obj" / ("V" + module), "rtl")
    if args.tools is not None:
        binary = args.tools.resolve() / "bin"
        liberty = args.pdk.resolve() / "ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
        with (work / "design.v").open("w") as output:
            execute([str(binary / "sv2v"), *map(str, rtl_files)], stdout=output, check=True, timeout=120)
        def quote(path):
            if any(char in str(path) for char in ('"', '\\', '\n')):
                raise ValueError("unsupported synthesis path")
            return '"' + str(path) + '"'
        frontend_checked = filter_case
        if frontend_checked:
            # Catch signed arithmetic changes before paying for technology
            # mapping and a much larger gate-model compilation.
            (work / "frontend.ys").write_text(f'''read_verilog {quote(work / 'design.v')}
hierarchy -check -top {module}
proc
opt
memory
opt
write_verilog {quote(work / 'frontend.v')}
''')
            with (work / "frontend.log").open("w") as log:
                execute([str(binary / "yosys"), "-Q", "-T", "-s", str(work / "frontend.ys")],
                        stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
            with (work / "frontend-build.log").open("w") as log:
                execute([str(args.verilator), "--cc", "--exe", "--build", "--no-skip-identical",
                         "--output-groups", "0", "-j", "2", "--top-module", module, "-Wno-fatal",
                         "--Mdir", str(work / "frontend_obj"), "-CFLAGS", "-DVERILATOR -DMAPPED -std=c++17" + defines,
                         str(work / "frontend.v"), str(harness), "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx],
                        stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
            execute([str(work / "frontend_obj" / ("V" + module)), *map(str, fixture_args), str(work / "frontend_trace.csv")],
                    check=True, timeout=60)
            if not filecmp.cmp(work / "cpp_trace.csv", work / "frontend_trace.csv", shallow=False):
                raise ValueError("native/Yosys frontend trajectories differ")
        script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top {module}
synth -top {module} -flatten -noabc {"-noshare" if args.case in ("trellis", "receiver") or filter_case else ""}
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(binary / 'abc')} -liberty {quote(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(work / 'cells.v')}
'''
        (work / "synth.ys").write_text(script)
        with (work / "synthesis.log").open("w") as log:
            execute([str(binary / "yosys"), "-Q", "-T", "-s", str(work / "synth.ys")],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=stage_timeout)
        mapped = json.loads((work / "mapped.json").read_text())["modules"][module]
        counts = Counter(cell["type"] for cell in mapped["cells"].values())
        if not counts or any(not cell.startswith("sg13g2_") for cell in counts):
            raise ValueError("hardware synthesis left non-IHP or blackbox cells")
        command = [str(args.verilator), "--cc", "--exe", "--build", "--no-skip-identical", "-j",
                   "2", "--top-module", module,
                   "--flatten", "--output-groups", "0", "--output-split", "10000", "--output-split-cfuncs", "500",
                   "-Wno-fatal", "--Mdir", str(work / "mapped_obj"), "-CFLAGS", "-DVERILATOR -DMAPPED -std=c++17" + defines,
                   str(work / "mapped.v"), str(work / "cells.v"), str(harness),
                   "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx +
                   (" OPT_FAST=-O0 OPT_SLOW=-O0" if filter_case else "")]
        with (work / "mapped-build.log").open("w") as log:
            execute(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=stage_timeout)
        execute([str(work / "mapped_obj" / ("V" + module)), *map(str, fixture_args), str(work / "mapped_trace.csv")],
                       check=True, timeout=1200 if args.case == "autoneg_link" else 180 if filter_case else 60)
        if not filecmp.cmp(work / "cpp_trace.csv", work / "mapped_trace.csv", shallow=False):
            raise ValueError("native/IHP mapped output trajectories differ")
        if args.case == "receiver":
            check_receiver_faults(work / "mapped_obj" / ("V" + module), "mapped")
        if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != value for path, value in source_hashes.items()):
            raise ValueError("hardware sources changed while validation was running")
        if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != value for path, value in fixture_hashes.items()):
            raise ValueError("hardware fixtures changed while validation was running")
        (work / "hardware_checks.json").write_text(json.dumps(dict(status="pass", scope="functional equivalence",
            module=module, mapped_sha256=hashlib.sha256((work / "mapped.v").read_bytes()).hexdigest(),
            source_sha256=source_hashes,
            fixture_sha256=fixture_hashes,
            cells=counts, native_rtl_exact=True, native_frontend_exact=True if frontend_checked else None,
            native_mapped_exact=True), indent=2) + "\n")
        print("IHP mapped-gate trajectory matches exactly.")


if __name__ == "__main__":
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("alignment", "pcs", "scrambler", "transmit_bits", "transmitter",
                                          "branch_metrics", "branch_metrics_pipeline", "trellis", "synchronize", "receiver", "framer", "filter",
                                          "filter_pipeline", "filter_datapath", "timing", "adc_sar", "flp_tx", "flp_rx",
                                          "master_slave", "negotiation", "autoneg_link"), required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--tools", type=Path)
    parser.add_argument("--pdk", type=Path)
    parser.add_argument("--verilator", type=Path, required=True)
    parser.add_argument("--cxx", default="/usr/bin/c++")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if args.tools is not None and args.pdk is None:
        parser.error("--tools requires --pdk")
    run(args)
