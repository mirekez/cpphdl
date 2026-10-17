#!/usr/bin/env python3
"""C++RTL -> IHP standard cells -> routed core GDSII with real DRC/LVS gates.

The chip target reports the missing physical PHY work and refuses to label the
digital core as a complete copper repeater. No behavioral analog model is used
as a substitute for physical cells.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from checks import chip_blockers

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[2]
TESTS = SOURCE / "tests"


def quoted(path):
    text = str(path)
    if any(value in text for value in ('"', '\\', '\n', '\r')):
        raise ValueError("unsupported Yosys path quoting: " + text)
    return '"' + text + '"'


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report = dict(target=args.target, status="running", full_1000base_t_chip=False, stages=[])
    result_path = work / "flow.json"

    def save():
        result_path.write_text(json.dumps(report, indent=2) + "\n")

    def stage(name, command, output=None, timeout=600):
        print(name, flush=True)
        started = time.monotonic()
        log = work / (name + ".log")
        with log.open("w") as file:
            if output:
                with output.open("w") as stdout:
                    process = subprocess.run([str(x) for x in command], cwd=work, env=environment,
                                             stdout=stdout, stderr=file, timeout=timeout)
            else:
                process = subprocess.run([str(x) for x in command], cwd=work, env=environment,
                                         stdout=file, stderr=subprocess.STDOUT, timeout=timeout)
        report["stages"].append(dict(name=name, seconds=round(time.monotonic() - started, 3),
                                     returncode=process.returncode, log=str(log)))
        save()
        if process.returncode:
            raise RuntimeError(f"{name} failed: inspect {log}")

    save()
    try:
        # A stale successful GDS must never be advertised after a failed new run.
        (work / "repeater_core.gds").unlink(missing_ok=True)
        if args.target == "chip":
            report.update(status="blocked", missing=chip_blockers())
            save()
            print("Full-chip implementation unavailable:\n- " + "\n- ".join(report["missing"]))
            return 2
        tools = args.tools.resolve()
        binary = tools / "bin"
        for tool in ("yosys", "abc", "sv2v", "openroad", "klayout", "iverilog", "vvp"):
            if not (binary / tool).is_file():
                raise ValueError(f"missing {tool}: run prepare_tools.py --destination {tools}")
        pdk = args.pdk.resolve()
        python = pdk / ".cpphdl/venv/bin/python"
        if not python.is_file():
            raise ValueError("PDK Python environment missing: run ../.prepare_IHPSG13G2.sh")
        converter = args.cpphdl.resolve()
        if not converter.is_file():
            raise ValueError("build the root cpphdl converter first")
        if not shutil.which("verilator"):
            raise ValueError("Verilator is required to verify the generated RTL")
        environment = dict(os.environ)
        environment.update(REPEATER_PDK=str(pdk), REPEATER_WORK=str(work), REPEATER_SOURCE=str(SOURCE),
                           REPEATER_TOOLS=str(tools), PATH=str(binary) + os.pathsep + environment.get("PATH", ""))
        libroot = pdk / "ihp-sg13g2/libs.ref/sg13g2_stdcell"
        liberty = libroot / "lib/sg13g2_stdcell_typ_1p20V_25C.lib"
        generated = work / "rtl"
        generated.mkdir(exist_ok=True)
        for name, flag in (("openroad", "-version"), ("yosys", "-V"), ("klayout", "-v"), ("sv2v", "--version")):
            stage("version-" + name, [binary / name, flag])
        stage("compile-native", [args.cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-I" + str(REPO / "include"),
                                 TESTS / "core.cpp", "-o", work / "core_test"])
        stage("native", [work / "core_test", work / "native_vectors.csv"])
        stage("vectors-check", [sys.executable, TESTS / "check_vectors.py", work / "native_vectors.csv"])
        stage("export", [converter, "--primary_clock", "ref_clock", "125000000", "--generated-dir", generated,
                         SOURCE / "repeater.cpp", "--", "-I" + str(REPO / "include")])
        rtl = [generated / "Predef_pkg.sv", generated / "RepeaterCore.sv"]
        stage("verilate", ["verilator", "--cc", "--exe", "--no-skip-identical", "--top-module", "RepeaterCore", "-Wno-fatal",
                           "--Mdir", work / "verilator", "-CFLAGS", "-DVERILATOR -std=c++17", *rtl, TESTS / "core.cpp"])
        stage("build-rtl-test", ["make", "-C", work / "verilator", "-f", "VRepeaterCore.mk", "-j2",
                                 "CXX=" + args.cxx, "LINK=" + args.cxx])
        stage("rtl-test", [work / "verilator/VRepeaterCore"])
        stage("sv2v", [binary / "sv2v", *rtl], output=work / "RepeaterCore.v")
        script = f"""read_liberty -lib {quoted(liberty)}
read_verilog {quoted(work / 'RepeaterCore.v')}
hierarchy -check -top RepeaterCore
synth -top RepeaterCore -flatten -noabc
dfflibmap -liberty {quoted(liberty)}
abc -exe {quoted(binary / 'abc')} -liberty {quoted(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
stat -liberty {quoted(liberty)}
write_verilog -noattr -noexpr {quoted(work / 'mapped.v')}
write_json {quoted(work / 'mapped.json')}
"""
        (work / "synth.ys").write_text(script)
        stage("synth", [binary / "yosys", "-Q", "-T", "-s", work / "synth.ys"])
        mapped = json.loads((work / "mapped.json").read_text())["modules"]["RepeaterCore"]
        counts = Counter(cell["type"] for cell in mapped["cells"].values())
        if not counts or any(not name.startswith("sg13g2_") for name in counts):
            raise ValueError("synthesis left unmapped/blackbox cells")
        if sum(count for name, count in counts.items() if name.startswith("sg13g2_df")) != 21:
            raise ValueError("synthesis lost forwarding state")
        report["mapped_cells"] = counts
        # Derive functional cell models from the real Liberty. The pinned PDK's
        # Verilog delayed timing-check signals are not supported by Icarus 11.
        (work / "cells.ys").write_text(f"read_liberty -ignore_miss_func -ignore_miss_dir {quoted(liberty)}\n"
                                      f"write_verilog -noattr {quoted(work / 'cells_functional.v')}\n")
        stage("cell-models", [binary / "yosys", "-Q", "-T", "-s", work / "cells.ys"])

        def gate_test(name, netlist):
            stage(name + "-compile", [binary / "iverilog", "-g2012", "-s", "test", "-o", work / name,
                                      TESTS / "gates.v", netlist, work / "cells_functional.v"])
            stage(name, [binary / "vvp", work / name])

        gate_test("mapped-test", work / "mapped.v")
        stage("routing-technology", [sys.executable, SOURCE / "route_tech.py", "--pdk", pdk, "--work", work])
        stage("place-route-extract", [binary / "openroad", "-no_init", "-exit", SOURCE / "place_route.tcl"])
        gate_test("postroute-test", work / "postroute.v")
        stage("timing-check", [python, SOURCE / "checks.py", "timing", work])
        stage("stream", [python, SOURCE / "stream.py", "--pdk", pdk, "--work", work])
        stage("fill", [binary / "klayout", "-zz", "-r", SOURCE / "fill_core.py", "-rd",
                       "output_file=" + str(work / "repeater_core_filled.gds"), work / "repeater_core_unfilled.gds"])
        verification = pdk / "ihp-sg13g2/libs.tech/klayout/tech"
        for directory in (work / "drc", work / "lvs"):
            if directory.exists():
                shutil.rmtree(directory)
        stage("drc", [python, verification / "drc/run_drc.py", "--path", work / "repeater_core_filled.gds",
                      "--topcell", "RepeaterCore", "--run_dir", work / "drc", "--mp", "2", "--density_thr", "2", "--antenna"])
        stage("drc-check", [python, SOURCE / "checks.py", "drc",
                            work / "drc/repeater_core_filled_RepeaterCore_full.lyrdb"])
        (work / "lvs.cir").write_text("* Physical core with actual transistor schematics\n"
                                    f'.include "{libroot / "cdl/sg13g2_stdcell.cdl"}"\n'
                                    f'.include "{work / "core.cdl"}"\n')
        stage("lvs", [python, verification / "lvs/run_lvs.py", "--layout", work / "repeater_core_filled.gds",
                      "--netlist", work / "lvs.cir", "--topcell", "RepeaterCore", "--run_mode", "deep", "--run_dir", work / "lvs"])
        stage("lvs-check", [python, SOURCE / "checks.py", "lvs", work / "lvs/repeater_core_filled.lvsdb"])
        shutil.copyfile(work / "repeater_core_filled.gds", work / "repeater_core.gds")
        report.update(status="core_checks_pass", gds=str(work / "repeater_core.gds"),
                      timing=json.loads((work / "timing-check.log").read_text()),
                      scope="digital forwarding core only; no copper PHYs, padframe or post-fill extraction",
                      missing_for_chip=chip_blockers())
        report["sha256"] = {str(path.relative_to(work)): hashlib.sha256(path.read_bytes()).hexdigest()
                            for path in (work / "repeater_core.gds", work / "mapped.v", work / "physical.v",
                                         work / "core.cdl", work / "core.spef", work / "core.def", work / "route_tech.lef")}
        report["input_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in (SOURCE / "repeater.hpp", SOURCE / "constraints.sdc",
                                               SOURCE / "place_route.tcl", SOURCE / "fill_core.py", SOURCE / "route_tech.py", liberty,
                                               libroot / "lef/sg13g2_tech.lef", libroot / "lef/sg13g2_stdcell.lef",
                                               libroot / "gds/sg13g2_stdcell.gds", libroot / "cdl/sg13g2_stdcell.cdl",
                                               libroot / "lib/sg13g2_stdcell_slow_1p08V_125C.lib",
                                               libroot / "lib/sg13g2_stdcell_fast_1p32V_m40C.lib", converter,
                                               tools / "rcx_patterns.rules")}
        save()
        print("Core checks passed:", report["gds"])
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report.update(status="failed", error=str(error))
        save()
        print(str(error), file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("core", "chip"), default="chip")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--pdk", type=Path, default=SOURCE.parent / "IHPSG13G2")
    parser.add_argument("--tools", type=Path, default=SOURCE.parents[1] / "out/repeater-toolchain")
    parser.add_argument("--cpphdl", type=Path, default=REPO / "build/cpphdl")
    parser.add_argument("--cxx", default="/usr/bin/c++")
    args = parser.parse_args()
    if args.work is None:
        args.work = SOURCE / "results" / args.target
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
