#!/usr/bin/env python3
"""Independent electrical checks per flow, sampled C++ DSP, then solver comparison."""
import argparse
import bisect
import cmath
import csv
import json
import math
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "VerilogA"))
from compare import read_cpp, read_spice


class Trace:
    def __init__(self, folder, flow):
        self.folder = folder
        self.contract = json.loads((folder / "contract.json").read_text())
        self.conditions = json.loads((folder / "conditions.json").read_text())
        with (folder / "columns.csv").open(newline="") as file:
            self.header = next(csv.reader(file))
        if flow == "cpp":
            header, self.rows = read_cpp(folder / "cpp/trace.csv")
            if header != self.header:
                raise ValueError("native trace interface differs from exported observations")
        else:
            self.rows = read_spice(folder / "spice.dat", self.header)
        self.times = [row[0] for row in self.rows]
        self.ui = self.contract["ui_s"]
        self.results = []
        if abs(self.times[-1] - self.contract["stop_s"]) > self.ui * 1e-5:
            raise ValueError("truncated simulation output")
        if self.times[0] > self.ui / 16 or any(b - a > self.ui / 16 for a, b in zip(self.times, self.times[1:])):
            raise ValueError("missing initialization or insufficient trajectory coverage")

    def at(self, name, time):
        if not self.times[0] <= time <= self.times[-1]:
            raise ValueError("measurement outside simulation output")
        i = min(max(1, bisect.bisect_left(self.times, time)), len(self.rows) - 1)
        column = self.header.index(name)
        a, b = self.rows[i - 1], self.rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    def check(self, name, value, low, high):
        self.results.append(dict(name=name, value=value, low=low, high=high,
                                 passed=math.isfinite(value) and low <= value <= high))


def solve(matrix, rhs):
    """Small independent DC nodal solution, separate from the C++ transient backend."""
    a = [list(row) + [rhs[i]] for i, row in enumerate(matrix)]
    for k in range(len(a)):
        pivot = max(range(k, len(a)), key=lambda row: abs(a[row][k]))
        a[k], a[pivot] = a[pivot], a[k]
        scale = a[k][k]
        if abs(scale) < 1e-20:
            raise ValueError("singular DC reference circuit")
        a[k] = [value / scale for value in a[k]]
        for row in range(len(a)):
            if row != k:
                factor = a[row][k]
                a[row] = [x - factor * y for x, y in zip(a[row], a[k])]
    return [row[-1] for row in a]


def dc_link(commands, lanes, loaded):
    count = 2 * lanes
    matrix = [[0.] * count for _ in range(count)]
    rhs = [0.] * count
    for side in range(2):
        source = 35 if loaded and side else 50
        for lane in range(lanes):
            node = side * lanes + lane
            matrix[node][node] += 1 / source + 1 / 15
            matrix[node][(1 - side) * lanes + lane] -= 1 / 15
            rhs[node] = commands[node] / source
            if lanes == 4:
                for neighbor in ((lane - 1) % 4, (lane + 1) % 4):
                    matrix[node][node] += 1 / 5000
                    matrix[node][side * lanes + neighbor] -= 1 / 5000
    return solve(matrix, rhs)


def system_checks(t, dsp):
    c = t.contract
    name = c["case"]
    lanes = 1 if name == "pair_link" else 4
    tx_error = line_error = receiver_error = 0
    for i in range(8, c["count"] - 1):
        time = (i + c["sample_phase"]) * t.ui
        commands = [.5 * (.75 * c[side][i][lane] + .25 * c[side][i - 1][lane])
                    for side in ("A", "B") for lane in range(lanes)]
        expected_line = dc_link(commands, lanes, name == "link_loaded")
        for side_index, side in enumerate(("A", "B")):
            for lane in range(lanes):
                node = side_index * lanes + lane
                tx_error = max(tx_error, abs(t.at(f"TX{side}{lane}", time) - commands[node]))
                line_error = max(line_error, abs(t.at(f"LINE{side}{lane}", time) - expected_line[node]))
                expected_rx = 1.15 * (expected_line[node] - .35 * commands[node])
                receiver_error = max(receiver_error, abs(t.at(f"RX{side}{lane}", time) - expected_rx))
    t.check("partial-response transmit error V", tx_error, 0, .003)
    t.check("reciprocal loaded cable DC error V", line_error, 0, .004)
    t.check("loaded hybrid/equalizer error V", receiver_error, 0, .012)
    samples = t.folder / "adc-input.csv"
    with samples.open("w", newline="") as file:
        writer = csv.writer(file)
        header = ["symbol"]
        for channel in range(2 * lanes):
            header += [f"rx{channel}", *(f"local{channel}_{j}" for j in range(4)), f"desired{channel}"]
        writer.writerow(header)
        for i in range(c["count"]):
            row = [i]
            time = (i + c["sample_phase"]) * t.ui
            for side, other in (("A", "B"), ("B", "A")):
                references = [t.at(f"TX{side}{lane}", time) / .5 for lane in range(lanes)]
                references += [0] * (4 - lanes)
                for lane in range(lanes):
                    row += [t.at(f"RX{side}{lane}", time) / .25, *references, c[other][i][lane]]
            writer.writerow(row)
    subprocess.run([str(dsp), str(samples), str(t.folder / "dsp.json"), str(c["training_symbols"]),
                    "0" if name == "link_uncancelled" else "1"], check=True)
    result = json.loads((t.folder / "dsp.json").read_text())
    errors = sum(channel["symbol_errors"] for channel in result["channels"])
    raw_errors = sum(channel["raw_errors"] for channel in result["channels"])
    worst_mse = max(channel["mse"] for channel in result["channels"])
    improvement = min(10 * math.log10(channel["raw_mse"] / max(channel["mse"], 1e-20))
                      for channel in result["channels"])
    scored = 2 * lanes * (c["count"] - c["training_symbols"] - 8)
    if name == "link_uncancelled":
        t.check("disabled adaptation exposes receiver errors", errors, 40, scored)
    else:
        t.check("independent holdout PAM5 symbol errors", errors, 0, 0)
        t.check("worst holdout MSE in normalized levels", worst_mse, 0, .025)
        t.check("minimum echo/NEXT/FFE improvement dB", improvement, 10, 80)
    t.check("ADC clipping count", sum(channel["adc_clips"] for channel in result["channels"]), 0, 0)
    t.check("raw detector demonstrates the cancellation problem", raw_errors, 8, scored)
    return dict(symbol_errors=errors, raw_errors=raw_errors, worst_mse=worst_mse,
                minimum_improvement_db=improvement, sampled_receivers=2 * lanes)


def evaluate(t, dsp):
    c = t.contract
    name, a, b = c["case"], c["A"], c["B"]
    metrics = {}
    if name.startswith("link") or name == "pair_link":
        metrics = system_checks(t, dsp)
    elif name.startswith("ctle"):
        frequency = 125e6 / (16 if name == "ctle_low" else 2)
        begin, finish = (14 if name == "ctle_low" else 22) * t.ui, 62 * t.ui
        count = 2048
        phasor = sum(t.at("OUT", begin + (i + .5) * (finish - begin) / count) *
                     cmath.exp(-2j * math.pi * frequency * (begin + (i + .5) * (finish - begin) / count))
                     for i in range(count)) * 2j / count / .02
        s = 2j * math.pi * frequency
        expected = 2.3 * 1000 * (1 + s * 1000 * 1e-12) / (2000 + s * 1e6 * 1e-12) / (1 + s * 1000 * 50e-15)
        relative_error = abs(phasor - expected) / abs(expected)
        t.check("independent CTLE complex transfer error", relative_error, 0, .02)
        metrics.update(gain=abs(phasor), phase_rad=cmath.phase(phasor))
    else:
        error = 0
        decision_errors = 0
        levels = set()
        for i in range(8, c["count"] - 1):
            time = (i + .8) * t.ui
            level, previous, remote = a[i][0], a[i - 1][0], b[i][0]
            levels.add(level)
            if name in ("dac", "dac_loaded", "memory", "pulse_shaper", "hybrid", "hybrid_bad", "receiver"):
                expected = {"dac": .5 * level, "dac_loaded": .25 * level, "memory": .25 * previous,
                            "pulse_shaper": .5 * (.75 * level + .25 * previous),
                            "hybrid": .25 / 1.15 * remote,
                            "hybrid_bad": .25 / 1.15 * remote + .2 * level,
                            "receiver": .25 * remote}[name]
                error = max(error, abs(t.at("OUT", time) - expected))
            elif name in ("mos_driver", "biased_driver", "trimmed_driver"):
                error = max(error, abs(t.at("DIFF", time) - .25 * level),
                            abs((t.at("P_ABS", time) + t.at("N_ABS", time)) / 2 - 2.8))
            elif name in ("driver", "driver_loaded"):
                expected = (.25 if name == "driver" else 1 / 6) * level
                error = max(error, abs(t.at("DIFF", time) - expected))
                error = max(error, abs((t.at("P", time) + t.at("N", time)) / 2))
            elif name == "transmitter":
                command = .5 * (.75 * level + .25 * previous)
                error = max(error, abs(t.at("TARGET", time) - command), abs(t.at("DIFF", time) - command / 2))
            elif name == "slicer":
                for bit in range(4):
                    expected = 1.2 if level + 2 > bit else 0
                    error = max(error, abs(t.at(f"C{bit}", time) - expected))
            elif name.startswith("channel"):
                near, far = (50 / 115, 65 / 115) if name == "channel_reverse" else (
                    (.5, .35) if name == "channel_loaded" else (65 / 115, 50 / 115))
                error = max(error, abs(t.at("NEAR", time) - near * .5 * level),
                            abs(t.at("FAR", time) - far * .5 * level))
            elif name.startswith("dfe"):
                recovered = int(max(-2, min(2, math.floor(t.at("OUT", time) / .25 + .5))))
                decision_errors += recovered != level
                if name == "dfe":
                    error = max(error, abs(t.at("OUT", time) - .25 * level))
            else:
                raise ValueError("missing checker for " + name)
        t.check("all five PAM levels exercised", len(levels), 5, 5)
        t.check("absolute plateau/transfer error V", error, 0, .006)
        if name.startswith("dfe"):
            t.check("PAM5 decision errors", decision_errors, 5 if name == "dfe_bypass" else 0,
                    64 if name == "dfe_bypass" else 0)
            metrics["symbol_errors"] = decision_errors
    return dict(status="pass" if all(item["passed"] for item in t.results) else "fail",
                case=name, checks=t.results, metrics=metrics)


def compare(left, right):
    for folder in (left, right):
        if json.loads((folder / "checks.json").read_text())["status"] != "pass":
            raise ValueError("both flows must pass their independent checks before comparison")
    a, b = Trace(left, "cpp"), Trace(right, "veriloga")
    if a.header != b.header or a.contract != b.contract:
        raise ValueError("stimuli, observations or contract differ between flows")
    results = []
    with (left.parent / "difference.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(a.header)
        squared = [0.] * (len(a.header) - 1)
        peak = [0.] * len(squared)
        count = (a.contract["count"] - 5) * 64
        for i in range(count):
            time = (4 + i / 64) * a.ui
            errors = [a.at(label, time) - b.at(label, time) for label in a.header[1:]]
            writer.writerow([time, *errors])
            for column, error in enumerate(errors):
                squared[column] += error * error
                peak[column] = max(peak[column], abs(error))
        for column, label in enumerate(a.header[1:]):
            rms = math.sqrt(squared[column] / count)
            tolerance = .06 if label.startswith("C") else .02 if a.contract["case"].startswith("dfe") else .012
            results.append(dict(name=label + " waveform RMS V", value=rms, tolerance=tolerance,
                                peak_V=peak[column], passed=rms <= tolerance))
    if (left / "dsp.json").is_file():
        x = json.loads((left / "dsp.json").read_text())["channels"]
        y = json.loads((right / "dsp.json").read_text())["channels"]
        for channel, (first, second) in enumerate(zip(x, y)):
            error = max(abs(v - w) for v, w in zip(first["weights"], second["weights"]))
            results.append(dict(name=f"receiver {channel} trained coefficient agreement", value=error,
                                tolerance=.02, passed=error <= .02))
    result = dict(status="pass" if all(item["passed"] for item in results) else "fail", checks=results)
    (left.parent / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("cpp", "veriloga", "compare"))
    parser.add_argument("folder", type=Path)
    parser.add_argument("other", type=Path, nargs="?")
    parser.add_argument("--dsp", type=Path)
    args = parser.parse_args()
    try:
        if args.mode == "compare":
            result = compare(args.folder, args.other)
        else:
            result = evaluate(Trace(args.folder, args.mode), args.dsp)
            (args.folder / "checks.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        if result["status"] != "pass":
            raise ValueError("declared 1G checks failed")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
