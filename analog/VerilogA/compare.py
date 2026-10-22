#!/usr/bin/env python3
"""Normalize ngspice data and compare independently integrated trajectories (standard library only)."""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path


def checked_rows(rows, width, label):
    result = []
    for row in rows:
        if len(row) != width:
            raise ValueError(f"{label}: expected {width} columns, got {len(row)}")
        value = [float(x) for x in row]
        if not all(math.isfinite(x) for x in value):
            raise ValueError(f"{label}: nonfinite or missing sample")
        if result and value[0] <= result[-1][0]:
            raise ValueError(f"{label}: non-increasing time")
        result.append(value)
    if len(result) < 10:
        raise ValueError(f"{label}: incomplete trajectory")
    return result


def read_cpp(path):
    with path.open(newline="") as file:
        reader = csv.reader(file)
        header = next(reader)
        return header, checked_rows(reader, len(header), str(path))


def read_spice(path, header):
    with path.open() as file:
        names = next(file).split()
        if len(names) != len(header) or names[0] != "time":
            raise ValueError("ngspice output header does not match electrical observation count")
        return checked_rows((line.split() for line in file if line.strip()), len(header), str(path))


def interpolate(rows, times, time, column):
    if time < times[0] - 1e-15 or time > times[-1] + 1e-15:
        raise ValueError("reference trajectory does not cover a requested time")
    i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
    a, b = rows[i - 1], rows[i]
    fraction = (time - a[0]) / (b[0] - a[0])
    return a[column] + fraction * (b[column] - a[column])


def errors(native, reference):
    times = [r[0] for r in reference]
    errors = [0.0] * (len(native[0]) - 1)
    count = 0
    for row in native:
        if row[0] < times[0]:
            continue  # SPICE UIC starts at a positive time; no invented t=0 reference value.
        count += 1
        for col in range(1, len(row)):
            errors[col - 1] = max(errors[col - 1], abs(row[col] - interpolate(reference, times, row[0], col)))
    if count < 10:
        raise ValueError("too few overlapping native/reference samples")
    return errors


def window(rows, col, start, end):
    values = [r[col] for r in rows if start <= r[0] <= end]
    if len(values) < 20:
        raise ValueError("missing measurement window")
    return min(values), max(values), sum(values) / len(values)


def edges(rows, col, level, start, end):
    result = []
    for a, b in zip(rows, rows[1:]):
        if a[col] < level <= b[col]:
            time = a[0] + (b[0] - a[0]) * (level - a[col]) / (b[col] - a[col])
            if start <= time <= end:
                result.append(time)
    return result


def pll_metrics(rows):
    ref = edges(rows, 1, .6, .01, .015)
    out = edges(rows, 2, .6, .01, .015)
    if len(ref) < 40 or len(out) < 40 or abs(len(ref) - len(out)) > 1:
        raise ValueError("PLL lost reference/output cycles")
    frequency = (len(out) - 1) / (out[-1] - out[0])
    first = min(range(len(ref)), key=lambda i: abs(ref[i] - out[0]))
    phase = [(edge - ref[i + first]) * 12000 for i, edge in enumerate(out) if i + first < len(ref)]
    if max(phase) - min(phase) > .05 or abs(frequency / 12000 - 1) > .01:
        raise ValueError("PLL trajectory does not demonstrate late frequency/phase lock")
    return [frequency, sum(phase) / len(phase), window(rows, 3, .01, .015)[2]]


def agc_metrics(rows):
    result = []
    for start in (.06, .14, .22):
        result += [window(rows, 1, start, start + .015)[1],
                   window(rows, 2, start, start + .015)[2],
                   window(rows, 3, start, start + .015)[2]]
    return result


def compare(case, coarse, fine, reference, header):
    if fine[-1][0] > reference[-1][0] + 1e-14 or reference[0][0] > fine[1][0]:
        raise ValueError("SPICE trace is truncated or misses initialization")
    if len(fine) < len(coarse) * 1.8:
        raise ValueError("native refinement did not increase the sample count")
    measurements = []
    if case in ("pll", "agc"):
        measure = pll_metrics if case == "pll" else agc_metrics
        a, b, target = measure(coarse), measure(fine), measure(reference)
        tolerances = [12, .01, .01] if case == "pll" else [.005, .06, .005] * 3
        labels = ["frequency_Hz", "phase_cycles", "control_V"] if case == "pll" else [
            f"{label}_{start}" for start in ("initial", "strong", "restored")
            for label in ("positive_peak_V", "control_V", "detector_V")]
        for label, x, y, z, tolerance in zip(labels, a, b, target, tolerances):
            measurements.append(dict(name=label, coarse=x, fine=y, reference=z, tolerance=tolerance,
                                     coarse_error=abs(x - z), fine_error=abs(y - z)))
    else:
        a, b = errors(coarse, reference), errors(fine, reference)
        tolerance = {"divider": 2e-6, "mos_regions": 2e-6, "rc": .011, "rc_initial": .011,
                     "floating_charge": .002, "inverter": .02, "opamp": .04,
                     "ldo": .02, "ldo_dropout": .02, "controlled_resistor": .01, "driver": .01}[case]
        for name, x, y in zip(header[1:], a, b):
            measurements.append(dict(name=name, coarse_error=x, fine_error=y, tolerance=tolerance))
    passed = True
    for item in measurements:
        # A numerical floor allows equal DC results and already converged observables.
        floor = max(2e-6, item["tolerance"] * .02)
        item["pass"] = (item["fine_error"] <= item["tolerance"] and
                        item["fine_error"] <= .85 * item["coarse_error"] + floor)
        passed &= item["pass"]
    # Independent analytic checks keep a common export/interpolation error from validating an RC mistake.
    if case in ("rc", "rc_initial"):
        for label, trajectory in (("native_fine", fine), ("spice", reference)):
            error = max(abs(r[1] - (math.exp(-r[0] / 1e-6) if case == "rc_initial" else
                                   1 - math.exp(-r[0] / 1e-6))) for r in trajectory)
            tolerance = .011 if label == "native_fine" else 1e-4
            measurements.append(dict(name=f"analytic_RC_{label}", error=error, tolerance=tolerance,
                                     **{"pass": error < tolerance}))
            passed &= error < tolerance
    return {"status": "sampled_pass" if passed else "failed", "case": case, "measurements": measurements,
            "samples": {"coarse": len(coarse), "fine": len(fine), "spice": len(reference)},
            "evidence": "selected-model cross-simulator and timestep-refinement checks; not a universal proof"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case")
    parser.add_argument("coarse", type=Path)
    parser.add_argument("fine", type=Path)
    args = parser.parse_args()
    output = args.fine / "comparison.json"
    try:
        header, coarse = read_cpp(args.coarse / "cpp/trace.csv")
        fine_header, fine = read_cpp(args.fine / "cpp/trace.csv")
        with (args.fine / "columns.csv").open(newline="") as file:
            source_header = next(csv.reader(file))
        if header != fine_header or header != source_header:
            raise ValueError("native and exported observation names/order differ")
        reference = read_spice(args.fine / "spice.dat", header)
        conditions = json.loads((args.fine / "conditions.json").read_text())
        stop = float(conditions["stop_s"])
        for label, trajectory in (("coarse", coarse), ("fine", fine), ("spice", reference)):
            if not math.isclose(trajectory[-1][0], stop, rel_tol=1e-10, abs_tol=1e-15):
                raise ValueError(f"{label} trace does not reach the declared stop time")
        with (args.fine / "spice.csv").open("w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(header)
            writer.writerows(reference)
        report = compare(args.case, coarse, fine, reference, header)
        # Common native timestamps make differences directly inspectable in spreadsheet/plot tools.
        times = [r[0] for r in reference]
        with (args.fine / "difference.csv").open("w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(["time_s"] + [name + "_cpp_minus_spice_V" for name in header[1:]])
            for row in fine:
                if row[0] >= times[0]:
                    writer.writerow([row[0]] + [row[col] - interpolate(reference, times, row[0], col)
                                                for col in range(1, len(row))])
        output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(json.dumps(report, indent=2))
        return 0 if report["status"] == "sampled_pass" else 1
    except (ValueError, OSError, StopIteration, KeyError) as error:
        report = {"status": "error", "error": str(error)}
        output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
