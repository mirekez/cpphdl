#!/usr/bin/env python3
"""Convert the actual C++ hardware class and run its contract against generated RTL."""
import argparse
import filecmp
from collections import Counter
import json
from pathlib import Path
import subprocess


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / "hardware_checks.json").unlink(missing_ok=True)
    source = Path(__file__).resolve().parents[1]
    repo = source.parents[1]
    clocks = ["--primary_clock", "ref_clk", "125000000", "--secondary_clock", "rx_clk", "125000000"] if args.case == "alignment" else []
    module = {"alignment": "ReceiveClockAligner", "pcs": "PCSSymbolMapper", "scrambler": "PCSSideStream"}[args.case]
    implementation = "receive_alignment.cpp" if args.case == "alignment" else "pcs.cpp"
    fixture_args = [source / "tests"] if args.case == "pcs" else []
    commands = [
        [args.native, *fixture_args, work / "cpp_trace.csv"],
        [args.converter, *clocks, "--generated-dir", work / "rtl", source / implementation, "--", "-I" + str(repo / "include")],
        [args.verilator, "--cc", "--exe", "--build", "--no-skip-identical", "-j", "2", "--top-module", module, "-Wno-fatal",
         "--Mdir", work / "obj", "-CFLAGS", "-DVERILATOR -std=c++17", work / "rtl/Predef_pkg.sv",
         work / "rtl" / (module + ".sv"), source / "tests" / (args.case + ".cpp"),
         "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx],
        [work / "obj" / ("V" + module), *fixture_args, work / "rtl_trace.csv"]]
    for index, command in enumerate(commands):
        with (work / f"stage{index}.log").open("w") as log:
            result = subprocess.run([str(value) for value in command], stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise RuntimeError((work / f"stage{index}.log").read_text())
    if args.case == "alignment":
        rtl = (work / "rtl/ReceiveClockAligner.sv").read_text()
        for clock in ("rx_clk", "ref_clk"):
            if f"always_ff @(posedge {clock})" not in rtl:
                raise ValueError("converter lost clock domain " + clock)
        if rtl.count('(* ASYNC_REG = "TRUE" *)') != 8:
            raise ValueError("converter lost pointer/fault synchronizer attributes")
    if not filecmp.cmp(work / "cpp_trace.csv", work / "rtl_trace.csv", shallow=False):
        raise ValueError("native/generated RTL output trajectories differ")
    print((work / "stage3.log").read_text())
    print("Native and RTL trajectories match exactly.")
    if args.tools is not None:
        binary = args.tools.resolve() / "bin"
        liberty = args.pdk.resolve() / "ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
        with (work / "design.v").open("w") as output:
            subprocess.run([str(binary / "sv2v"), str(work / "rtl/Predef_pkg.sv"),
                            str(work / "rtl" / (module + ".sv"))], stdout=output, check=True, timeout=120)
        def quote(path):
            if any(char in str(path) for char in ('"', '\\', '\n')):
                raise ValueError("unsupported synthesis path")
            return '"' + str(path) + '"'
        script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top {module}
synth -top {module} -flatten -noabc
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
            subprocess.run([str(binary / "yosys"), "-Q", "-T", "-s", str(work / "synth.ys")],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        mapped = json.loads((work / "mapped.json").read_text())["modules"][module]
        counts = Counter(cell["type"] for cell in mapped["cells"].values())
        if not counts or any(not cell.startswith("sg13g2_") for cell in counts):
            raise ValueError("hardware synthesis left non-IHP or blackbox cells")
        command = [str(args.verilator), "--cc", "--exe", "--build", "--no-skip-identical", "-j", "2", "--top-module", module,
                   "-Wno-fatal", "--Mdir", str(work / "mapped_obj"), "-CFLAGS", "-DVERILATOR -std=c++17",
                   str(work / "mapped.v"), str(work / "cells.v"), str(source / "tests" / (args.case + ".cpp")),
                   "-MAKEFLAGS", "CXX=" + args.cxx + " LINK=" + args.cxx]
        with (work / "mapped-build.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        subprocess.run([str(work / "mapped_obj" / ("V" + module)), *map(str, fixture_args), str(work / "mapped_trace.csv")],
                       check=True, timeout=60)
        if not filecmp.cmp(work / "cpp_trace.csv", work / "mapped_trace.csv", shallow=False):
            raise ValueError("native/IHP mapped output trajectories differ")
        (work / "hardware_checks.json").write_text(json.dumps(dict(status="pass", cells=counts,
            native_rtl_exact=True, native_mapped_exact=True), indent=2) + "\n")
        print("IHP mapped-gate trajectory matches exactly.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("alignment", "pcs", "scrambler"), required=True)
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
