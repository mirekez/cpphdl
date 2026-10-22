#!/usr/bin/env python3
"""Run the unary steering topology with IHP PSP HV MOS models and common PAM5 checks."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests"))
from verify import Trace, evaluate


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    for name in ("spice.dat", "headroom.dat", "checks.json", "ngspice.log"):
        (work / name).unlink(missing_ok=True)
    case = "trimmed_driver" if args.trim_code is not None else "biased_driver" if args.physical_bias else "mos_driver"
    subprocess.run([str(args.bench.resolve()), case, str(work), "export", "1"], check=True)
    contract = json.loads((work / "contract.json").read_text())
    technology = args.pdk.resolve() / "ihp-sg13g2"
    entry = technology / "libs.tech/ngspice/models/cornerMOShv.lib"
    if not entry.is_file():
        raise ValueError("IHP HV MOS corner library is missing")
    ui, stop = contract["ui_s"], contract["stop_s"]
    deck = ["IHP SG13G2 unary PAM5 current-steering output test",
            f'.lib "{entry}" {args.corner}', "VSUPPLY vdd 0 3.3",
            "RP p vdd 50", "RN n vdd 50", "RLOAD p n 100",
            "CP p vdd 0.5p", "CN n vdd 0.5p"]
    if args.physical_bias:
        deck += ["VBIAS_SENSE vdd vbias_supply 0", "XBIAS bias bias 0 0 sg13_hv_nmos w=100u l=0.45u"]
        if args.trim_code is None:
            deck += ["RBIAS vbias_supply bias 1480"]
        else:
            deck += ["RBIAS vbias_supply bias 1800"]
            for bit in range(6):
                gate = 0 if args.trim_code & (1 << bit) else 3.3
                deck += [f"VTRIM{bit} trim{bit} 0 {gate}",
                         f"XSW{bit} branch{bit} trim{bit} vbias_supply vbias_supply sg13_hv_pmos w=100u l=0.45u",
                         f"RTRIM{bit} branch{bit} bias {256000 / (1 << bit):.17g}"]
    for cell in range(4):
        for polarity in ("p", "n"):
            previous = (cell < 2) == (polarity == "n")
            points = [(0, 3.3 if previous else 0)]
            for i in range(contract["count"] + 2):
                high = (contract["A"][i][0] + 2 > cell) == (polarity == "n")
                if high != previous:
                    points += [((i + .15) * ui, 3.3 if previous else 0),
                               ((i + .19) * ui, 3.3 if high else 0)]
                    previous = high
            wave = " ".join(f"{time:.17g} {value:.17g}" for time, value in points)
            deck += [f"VG{polarity}{cell} g{polarity}{cell} 0 PWL({wave})",
                     f"X{polarity}{cell} {polarity} g{polarity}{cell} t{cell} 0 sg13_hv_nmos w=100u l=0.45u"]
        if args.physical_bias:
            deck += [f"XTAIL{cell} t{cell} bias 0 0 sg13_hv_nmos w=300u l=0.45u"]
        else:
            # Same supply-referenced ideal tail bias as the earlier C++ schematic.
            deck += [f"GTAIL{cell} t{cell} 0 vdd 0 {0.005 / 3.3:.17g}"]
    deck += [".options method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8",
             ".control", "set noaskquit", "set wr_singlescale", "set wr_vecnames", "set numdgt=17",
             f"tran {ui / 32:.17g} {stop:.17g} 0 {ui / 64:.17g}",
             "let difference = v(p) - v(n)", "wrdata spice.dat difference v(p) v(n)" + (" v(bias)" if args.physical_bias else ""),
             "let supply_current = -i(VSUPPLY)",
             "wrdata headroom.dat v(t0) v(t1) v(t2) v(t3) supply_current" + (" i(VBIAS_SENSE)" if args.physical_bias else ""), "quit", ".endc", ".end"]
    (work / "ihp.cir").write_text("\n".join(deck) + "\n")
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK="ihp-sg13g2",
                       SPICE_USERINIT_DIR=str(technology / "libs.tech/ngspice"))
    completed = subprocess.run([str(args.ngspice.resolve()), "-b", "ihp.cir"], cwd=work,
                               env=environment, text=True, capture_output=True, timeout=120)
    log = completed.stdout + completed.stderr
    (work / "ngspice.log").write_text(log)
    if completed.returncode or re.search(r"\b(error|fatal|unknown|failed|aborted)\b|not found|no such file|timestep too small", log, re.I):
        raise ValueError("IHP transistor simulation failed; inspect " + str(work / "ngspice.log"))
    # Identical observation interface and absolute level/common-mode checker as both prototype flows.
    trace = Trace(work, "veriloga")
    result = evaluate(trace, None)
    with (work / "headroom.dat").open() as file:
        next(file)
        headroom = [[float(value) for value in line.split()] for line in file if line.strip()]
    late = [row for row in headroom if row[0] > 8 * ui and .33 < (row[0] / ui) % 1 < .95]
    if not late:
        raise ValueError("no settled bias-current/headroom observations")
    trace.check("IHP minimum tail headroom V", min(min(row[1:5]) for row in late), .2, 3.3)
    trace.check("IHP maximum tail voltage V", max(max(row[1:5]) for row in late), .2, 3.3)
    # Account for reference-branch current and let bias/displacement currents
    # settle before checking the DC supply budget of the new mirror topology.
    supply_rows = [row for row in late if not args.physical_bias or (row[0] / ui) % 1 > .65]
    supply_error = max(abs(row[5] - (.02 + row[6] if args.physical_bias else .02)) for row in supply_rows)
    trace.check("IHP settled supply-current error A", supply_error, 0, .0005)
    settling_time = 2e-9 if args.physical_bias else 1e-9
    settling = max(abs(trace.at("DIFF", (i + .19) * ui + settling_time) - .25 * contract["A"][i][0])
                   for i in range(8, contract["count"] - 1))
    trace.check(f"IHP {settling_time * 1e9:g} ns settling error V", settling, 0, .015)
    result.update(checks=trace.results, corner=args.corner, trim_code=args.trim_code,
                  model="IHP sg13_hv_nmos PSP, W=100um L=0.45um; " + ("resistor/diode MOS bias, 3:1 tail mirrors" if args.physical_bias else "ideal tail bias"))
    result["status"] = "pass" if all(item["passed"] for item in trace.results) else "fail"
    (work / "checks.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if result["status"] != "pass":
        raise ValueError("IHP driver failed its common electrical contract")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench", type=Path, required=True)
    parser.add_argument("--pdk", type=Path, required=True)
    parser.add_argument("--ngspice", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--corner", choices=("mos_tt", "mos_ss", "mos_ff"), default="mos_tt")
    parser.add_argument("--physical-bias", action="store_true", help="Use resistor/diode MOS bias and transistor tails")
    parser.add_argument("--trim-code", type=int, choices=range(64), help="External six-bit physical resistor-bank setting")
    args = parser.parse_args()
    if args.trim_code is not None:
        args.physical_bias = True
    try:
        run(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
