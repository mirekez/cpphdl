#!/usr/bin/env python3
"""Compare coarse/fine trajectories within each backend and convergence across backends."""
import argparse
import json
import math
from pathlib import Path

from verify import Trace


def differences(first, second):
    if first.header != second.header or first.contract != second.contract:
        raise ValueError("refinement changed circuit observations or stimulus contract")
    count = (first.contract["count"] - 5) * 64
    squared = [0.] * (len(first.header) - 1)
    for i in range(count):
        time = (4 + i / 64) * first.ui
        for column, label in enumerate(first.header[1:]):
            error = first.at(label, time) - second.at(label, time)
            squared[column] += error * error
    return [math.sqrt(value / count) for value in squared]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("case", choices=("pair_link", "link"))
    args = parser.parse_args()
    results, traces = [], {}
    for flow in ("cpp", "veriloga"):
        pair = []
        for suffix in ("", "_refined"):
            folder = args.results / (args.case + suffix) / flow
            if json.loads((folder / "checks.json").read_text())["status"] != "pass":
                raise ValueError("refinement requires independently passing absolute checks")
            pair.append(Trace(folder, flow))
        key = "cpp_step_s" if flow == "cpp" else "spice_max_step_s"
        if not math.isclose(pair[0].conditions[key], 2 * pair[1].conditions[key], rel_tol=1e-10):
            raise ValueError("refinement did not halve the integration step")
        for label, rms in zip(pair[0].header[1:], differences(*pair)):
            results.append(dict(name=f"{flow} {label} coarse/fine RMS V", value=rms,
                                tolerance=.012, passed=rms <= .012))
        traces[flow] = pair
    coarse = differences(traces["cpp"][0], traces["veriloga"][0])
    fine = differences(traces["cpp"][1], traces["veriloga"][1])
    for label, before, after in zip(traces["cpp"][0].header[1:], coarse, fine):
        # A small voltage floor avoids demanding reduction below an already negligible discrepancy.
        bound = .85 * before + .0002
        results.append(dict(name=f"{label} cross-backend convergence RMS V", coarse=before,
                            value=after, tolerance=bound, passed=after <= bound))
    report = dict(status="pass" if all(item["passed"] for item in results) else "fail", checks=results)
    (args.results / f"{args.case}-refinement.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if report["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
