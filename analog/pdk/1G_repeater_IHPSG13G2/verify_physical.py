#!/usr/bin/env python3
"""Run the full IHP DRC and transistor LVS decks on an actual assembled layout.

A passing result establishes these two physical checks only. It cannot establish
PHY interoperability, analog performance, ESD qualification or extracted timing.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

from checks import check_drc, check_lvs

SOURCE = Path(__file__).resolve().parent


def schematic_inputs(path, seen=None):
    seen = set() if seen is None else seen
    path = path.resolve()
    if path in seen:
        return seen
    seen.add(path)
    for line in path.read_text().splitlines():
        match = re.match(r'\s*\.inc(?:lude)?\s+(?:"([^"]+)"|\x27([^\x27]+)\x27|(\S+))', line, re.I)
        if match:
            include = Path(next(value for value in match.groups() if value is not None))
            schematic_inputs(include if include.is_absolute() else path.parent / include, seen)
    return seen


def verify(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    layout, netlist = args.layout.resolve(), args.netlist.resolve()
    report = dict(status="running", top=args.top, full_1000base_t_chip=False,
                  layout=str(layout), schematic=str(netlist), checks={}, errors=[])
    result_path = work / "physical_checks.json"

    def save():
        result_path.write_text(json.dumps(report, indent=2) + "\n")

    save()
    try:
        if not layout.is_file() or not netlist.is_file():
            raise ValueError("both real GDS and transistor schematic are required")
        if args.top not in ("RepeaterChip", "RepeaterPadframe"):
            raise ValueError("use RepeaterChip or RepeaterPadframe, not a digital core top")
        chip_ports = [f"mdi_{port}_{pair}_{polarity}" for port in ("a", "b")
                      for pair in range(4) for polarity in ("p", "n")]
        chip_ports.extend(("ref_clock", "reset", "enable", "active", "vdd", "vss", "iovdd", "iovss"))
        if args.interface is None and args.top != "RepeaterChip":
            raise ValueError("the padframe requires an explicit interface contract")
        ports = json.loads(args.interface.read_text()) if args.interface else chip_ports
        if (not isinstance(ports, list) or not ports or
                any(not isinstance(port, str) or not port.strip() for port in ports) or
                len(set(port.upper() for port in ports)) != len(ports)):
            raise ValueError("interface must list unique, nonempty top-level ports")
        if args.top == "RepeaterChip":
            expected = set(chip_ports)
            if set(ports) != expected:
                raise ValueError("chip interface must contain exactly two four-pair MDIs, one reference, controls and supplies")
        technology = args.pdk.resolve() / "ihp-sg13g2/libs.tech/klayout/tech"
        inputs = schematic_inputs(netlist) | {layout, (args.interface or Path(__file__)).resolve()}
        inputs.update(technology.glob("lvs/**/*.lvs"))
        inputs.update(technology.glob("drc/**/*.drc"))
        report["input_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(inputs)}
        report["pdk_revision"] = (args.pdk.resolve() / ".cpphdl/revision.txt").read_text().strip()
        python = args.pdk.resolve() / ".cpphdl/venv/bin/python"
        environment = dict(os.environ, PATH=str(args.tools.resolve() / "bin") + os.pathsep + os.environ.get("PATH", ""))
        commands = {
            "drc": [python, technology / "drc/run_drc.py", "--path", layout, "--topcell", args.top,
                    "--run_dir", work / "drc", "--mp", "2", "--density_thr", "2", "--antenna"],
            "lvs": [python, technology / "lvs/run_lvs.py", "--layout", layout, "--netlist", netlist,
                    "--topcell", args.top, "--run_dir", work / "lvs", "--run_mode", args.mode]}
        # Run both checks even if one fails. Never infer a pass from a runner's
        # process exit code: some PDK versions return zero after an LVS mismatch.
        for kind, command in commands.items():
            try:
                directory = work / kind
                if directory.exists():
                    # Refuse stale evidence without deleting a user's report tree.
                    raise ValueError(f"use a fresh verification directory: {directory} already exists")
                with (work / (kind + ".log")).open("w") as log:
                    result = subprocess.run(list(map(str, command)), env=environment, stdout=log,
                                            stderr=subprocess.STDOUT, timeout=args.timeout)
                if result.returncode:
                    report["errors"].append(f"{kind} runner failed ({result.returncode}); inspect {work / (kind + '.log')}")
                if kind == "drc":
                    evidence = work / "drc" / f"{layout.stem}_{args.top}_full.lyrdb"
                    database = ET.parse(evidence).getroot()
                    items = database.find("items")
                    if items is not None:
                        report.setdefault("observations", {})["drc"] = dict(
                            violations=len(items), rules=Counter(item.findtext("category") for item in items))
                    report["checks"][kind] = check_drc(evidence)
                else:
                    evidence = work / "lvs" / f"{layout.stem}.lvsdb"
                    report["checks"][kind] = check_lvs(evidence, args.top, ports, 100)
            except Exception as error:
                # Third-party database readers can raise parser/API exceptions
                # beyond ValueError. Every such failure is failed evidence.
                report["errors"].append(f"{kind}: {type(error).__name__}: {error}")
            save()
        report["status"] = "physical_checks_pass" if not report["errors"] and set(report["checks"]) == {"drc", "lvs"} else "failed"
    except Exception as error:
        report["status"] = "failed"
        report["errors"].append(f"{type(error).__name__}: {error}")
    save()
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "physical_checks_pass" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--netlist", type=Path, required=True)
    parser.add_argument("--interface", type=Path, help="JSON list of exact ports; defaults to the 24-pin chip contract")
    parser.add_argument("--top", choices=("RepeaterChip", "RepeaterPadframe"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--pdk", type=Path, default=SOURCE.parent / "IHPSG13G2")
    parser.add_argument("--tools", type=Path, default=SOURCE.parents[1] / "out/repeater-toolchain")
    parser.add_argument("--mode", choices=("flat", "deep"), default="deep")
    parser.add_argument("--timeout", type=int, default=1800)
    return verify(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
