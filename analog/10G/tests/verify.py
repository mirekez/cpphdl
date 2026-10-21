#!/usr/bin/env python3
"""One electrical/protocol checker for both solvers, followed by independent comparison."""
import argparse
import bisect
import cmath
import csv
import json
import math
from pathlib import Path
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
                raise ValueError("native observation interface mismatch")
        else:
            self.rows = read_spice(folder / "spice.dat", self.header)
        self.times = [row[0] for row in self.rows]
        self.ui = self.contract["ui_s"]
        if abs(self.times[-1] - self.contract["stop_s"]) > self.ui * 1e-5:
            raise ValueError("truncated trajectory")
        if self.times[0] > self.ui / 64 or any(b - a > self.ui / 32 for a, b in zip(self.times, self.times[1:])):
            raise ValueError("missing initialization or insufficient sample coverage")
        self.results = []

    def at(self, name, time):
        if not self.times[0] <= time <= self.times[-1]:
            raise ValueError("measurement outside trajectory")
        column = self.header.index(name)
        i = min(max(1, bisect.bisect_left(self.times, time)), len(self.rows) - 1)
        a, b = self.rows[i - 1], self.rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    def check(self, name, value, low, high):
        passed = math.isfinite(value) and low <= value <= high
        self.results.append(dict(name=name, value=value, low=low, high=high, passed=passed))

    def digital(self, name, time, bit):
        value = self.at(name, time)
        return int(not ((1.08 <= value <= 1.24) if bit else (-.04 <= value <= .12)))

    def active_edges(self):
        col = self.header.index("CLOCK")
        edges = []
        for a, b in zip(self.rows, self.rows[1:]):
            if a[col] < .6 <= b[col]:
                time = a[0] + (b[0] - a[0]) * (.6 - a[col]) / (b[col] - a[col])
                if self.contract.get("acquisition_ui", 30) * self.ui < time < self.times[-1] - self.ui:
                    if self.at("CLOCK", time - .15 * self.ui) < .2 and self.at("CLOCK", time + .15 * self.ui) > 1:
                        edges.append(time)
        return edges


def evaluate(t):
    name = t.contract["case"]
    bits = t.contract["bits"]
    ui = t.ui
    metrics = {}
    if name == "primitives":
        error = 0
        # RL after a short linear voltage ramp: integrate the known forcing independently by fine quadrature.
        # After the ramp the exact exponential starts from the analytically integrated ramp current.
        start, rise, tau = ui, .05 * ui, 5e-9 / 100
        i_end = .01 * (1 - tau / rise * (1 - math.exp(-rise / tau)))
        for k in range(20, 110):
            time = k * ui / 10
            expected = 10 * (.01 + (i_end - .01) * math.exp(-(time - start - rise) / tau))
            error = max(error, abs(t.at("RL_SENSE", time) - expected))
        t.check("analytic RL transient error V", error, 0, .0005)
        t.check("gm DC V", t.at("GM", 10 * ui), .19999, .20001)
        t.check("diode load-line V", t.at("DIODE", 10 * ui), .27272, .27274)
        t.check("PWL ramp midpoint V", t.at("INPUT", (1 + .025) * ui), .4999, .5001)
    elif name in ("gm_stage", "limiter"):
        for label, time, expected in (("low", 4 * ui, -.4 if name == "gm_stage" else 0),
                                      ("high", 9 * ui, 1.6 if name == "gm_stage" else 1.2)):
            t.check(label + " V", t.at("OUT", time), expected - .025, expected + .025)
    elif name == "rectifier":
        error = max(abs(t.at("RECTIFIED", (i + .8) * ui) - .4 * 10000 / 10020) for i in range(4, 94))
        t.check("full-wave load-line error V", error, 0, .0001)
    elif name in ("sample_hold", "dff", "mux2", "xor", "history", "deserializer", "serializer"):
        errors = 0
        columns = 3 if name == "history" else 4 if name == "deserializer" else 1
        for i in range(8, 94):
            for lane in range(columns):
                bit = bits[i - lane]
                if name in ("mux2", "xor"):
                    bit = 1 - bit
                errors += t.digital("D" + str(lane) if columns > 1 else "OUT", (i + .8) * ui, bit)
            if name == "sample_hold":
                # Before the next opening, input has changed but the output must keep the previous decision.
                errors += t.digital("OUT", (i + .3) * ui, bits[i - 1])
            if name in ("mux2", "xor"):
                errors += t.digital("OUT", (i + .3) * ui, bits[i])
        t.check("electrical symbol/hold errors", errors, 0, 0)
        metrics["symbol_errors"] = errors
    elif name in ("emphasis", "transmitter"):
        error = 0
        for i in range(8, 94):
            indices = (i + 1, i, i - 1) if name == "emphasis" else (i, i - 1, i - 2)
            expected = .4 * sum(weight * (2 * bits[index] - 1) for weight, index in zip((-.1, .8, -.1), indices))
            error = max(error, abs(t.at("DIFF", (i + .8) * ui) - expected))
        t.check("loaded three-tap FIR error V", error, 0, .006)
        metrics["fir_error_V"] = error
    elif name == "channel":
        error = max(abs(t.at("DIFF", (i + .8) * ui) - .25 * (2 * bits[i] - 1)) for i in range(8, 94))
        t.check("terminated channel settled error V", error, 0, .005)
    elif name in ("sfi", "sfi_decay"):
        if name == "sfi":
            error = max(abs(t.at("DIFF", (i + .8) * ui) - .4 * 50 / 50.1 * (2 * bits[i] - 1)) for i in range(8, 94))
            t.check("AC-coupled 10G symbol error V", error, 0, .001)
        else:
            # Ideal source, two 100 nF blocks and two 50 Ohm bias resistors: tau = 5 us.
            tau, rise = 50.1 * 100e-9, .04 * ui
            ramp_end = .4 * 50 / 50.1 * tau / rise * (1 - math.exp(-rise / tau))
            error = max(abs(t.at("DIFF", i * ui) - ramp_end * math.exp(-(i * ui - 1.04 * ui) / tau))
                        for i in range(2, 90))
            t.check("independent high-pass decay error V", error, 0, .0005)
            t.check("DC is blocked after 18 time constants V", abs(t.at("DIFF", 94 * ui)), 0, 1e-6)
        error = max(abs((t.at("P", (i + .8) * ui) + t.at("N", (i + .8) * ui)) / 2 - .6)
                    for i in range(8, 94))
        t.check("receiver common mode error V", error, 0, .00002)
    elif name.startswith("ctle"):
        frequency = 1e9 if name == "ctle_low" else 10.3125e9 / 2
        # Correlate exactly an integer number of sine periods after startup.
        periods = 5 if name == "ctle_low" else 30
        finish = 95 * ui
        begin = finish - periods / frequency
        count = 4096
        phasor = sum(t.at("OUT", begin + (i + .5) / count * (finish - begin)) *
                     cmath.exp(-2j * math.pi * frequency * (begin + (i + .5) / count * (finish - begin)))
                     for i in range(count)) * 2j / count / .02
        s = 2j * math.pi * frequency
        expected = 4 * (50 * (1 + s * 200 * 400e-15)) / (250 + s * 200 * 50 * 400e-15) / (1 + s * 50 * 5e-15)
        relative_error = abs(phasor - expected) / abs(expected)
        t.check("analytic CTLE complex gain relative error", relative_error, 0, .025)
        metrics.update(gain=abs(phasor), phase_rad=cmath.phase(phasor))
    elif name in ("lock_monitor", "lock_monitor_bad"):
        values = [t.at("LOCK", (i + .3) * ui) for i in range(60, 94)]
        if name == "lock_monitor":
            t.check("qualified lock V", min(values), 1.08, 1.23)
        else:
            t.check("persistent phase mismatch deasserts lock V", max(values), -.02, .12)
    elif name == "hogge":
        average = lambda label: sum(t.at(label, (i + .5) * ui / 128) for i in range(32 * 128, 94 * 128)) / (62 * 128)
        error, reference = average("ERROR"), average("REFERENCE")
        t.check("reference pulse activity V", reference, .15, .4)
        t.check("known early-clock imbalance V", error - reference, -.04, -.005)
        metrics.update(pulse_imbalance_V=error - reference)
    elif name == "vco":
        edges = t.active_edges()
        t.check("oscillator cycles", len(edges), 55, 75)
        period = (edges[-1] - edges[0]) / (len(edges) - 1)
        t.check("free-running nominal period UI", period / ui, .9, 1.1)
        metrics["vco_period_ui"] = period / ui
        high = sum(t.at("CLOCK", edge + .25 * period) for edge in edges[:-1]) / (len(edges) - 1)
        low = sum(t.at("CLOCK", edge + .75 * period) for edge in edges[:-1]) / (len(edges) - 1)
        t.check("divided clock high plateau V", high, 1.08, 1.23)
        t.check("divided clock low plateau V", low, -.03, .12)
    elif name == "cdr_no_data":
        t.check("no-transition lock stays low V", max(t.at("LOCK", (i + .3) * ui) for i in range(40, 94)), -.02, .12)
        edges = t.active_edges()
        t.check("VCO holdover clock remains present", len(edges), 50, 78)
        metrics["vco_period_ui"] = (edges[-1] - edges[0]) / ((len(edges) - 1) * ui)
    elif name.startswith("cdr") or name in ("receiver", "link", "link_loaded"):
        edges = t.active_edges()
        t.check("recovered cycles", len(edges), int(t.times[-1] / ui) - t.contract["acquisition_ui"] - 8, int(t.times[-1] / ui) - t.contract["acquisition_ui"] + 2)
        if len(edges) < 2:
            raise ValueError("no usable recovered clock")
        mean_time = sum(edges) / len(edges)
        mean_index = (len(edges) - 1) / 2
        period = sum((i - mean_index) * (edge - mean_time) for i, edge in enumerate(edges)) / sum(
            (i - mean_index) ** 2 for i in range(len(edges)))
        if name == "cdr_ppm":
            t.check("steady recovered rate error ppm", abs(period / ui - 1) * 1e6, 0, 100)
        max_period_error = max(abs((b - a) / ui - 1) for a, b in zip(edges, edges[1:]))
        t.check("clock mean period error UI", abs(period / ui - 1), 0, .005)
        t.check("clock worst period error UI", max_period_error, 0, .15)
        offset = t.contract["phase_ui"]
        latency = 1.5 if name.startswith("link") else 0
        # Holdover without data has no observable phase reference. Check sample phase after reacquisition.
        phase_edges = [edge for edge in edges if name != "cdr_gap" or edge / ui >= 226]
        if len(phase_edges) < 2:
            raise ValueError("insufficient post-acquisition phase observations")
        phases = [((edge / ui - latency - offset) % 1) for edge in phase_edges]
        t.check("earliest sample phase UI", min(phases), .28, .85)
        t.check("latest sample phase UI", max(phases), .28, .85)
        if name == "cdr_gap":
            t.check("transition timeout deasserts lock V", t.at("LOCK", 120 * ui), -.02, .12)
            t.check("lock reacquires within 80 UI after transitions resume V",
                    min(t.at("LOCK", (i + .3) * ui) for i in range(226, 254)), 1.0, 1.23)
        else:
            t.check("qualified lock late V", min(t.at("LOCK", (i + .3) * ui) for i in range(int(t.times[-1] / ui) - 26, int(t.times[-1] / ui) - 2)), 1.0, 1.23)
        metrics.update(period_ui=period / ui, mean_phase_ui=sum(phases) / len(phases), cycles=len(edges))
        if not name.startswith("cdr"):
            errors = 0
            recovered = {}
            parallel_errors = 0
            for edge in edges:
                index = math.floor(edge / ui - latency)
                time = edge + .18 * ui
                errors += t.digital("Q", time, bits[index])
                recovered[index] = int(t.at("Q", time) > .6)
                for lane in range(4):
                    parallel_errors += t.digital("D" + str(lane), time, bits[index - 1 - lane])
            t.check("recovered serial errors", errors, 0, 0)
            t.check("parallel word errors", parallel_errors, 0, 0)
            metrics.update(symbol_errors=errors, parallel_errors=parallel_errors)
            if t.contract.get("framed"):
                expected = 0x0123456789abcdef
                scrambled = []
                for i in range(64):
                    scrambled.append(((expected >> i) & 1) ^ (scrambled[i - 39] if i >= 39 else 1) ^
                                     (scrambled[i - 58] if i >= 58 else 1))
                if bits[112:178] != [0, 1] + scrambled:
                    raise ValueError("stimulus does not match independently encoded 64b/66b data block")
                if any(i not in recovered for i in range(112, 178)):
                    raise ValueError("recovered stream is missing part of the complete 66-bit block")
                t.check("recovered data sync header", 2 * recovered[112] + recovered[113], 1, 1)
                encoded = [recovered[i] for i in range(114, 178)]
                decoded = sum((bit ^ (encoded[i - 39] if i >= 39 else 1) ^
                               (encoded[i - 58] if i >= 58 else 1)) << i for i, bit in enumerate(encoded))
                t.check("descrambled 64-bit payload errors", bin(decoded ^ expected).count("1"), 0, 0)
    elif name in ("dfe", "dfe_bypass"):
        errors = sum(t.digital("Q", (i + .8) * ui, bits[i]) for i in range(8, 94))
        t.check("decision errors", errors, 0 if name == "dfe" else 20, 0 if name == "dfe" else 80)
        metrics["symbol_errors"] = errors
        if name == "dfe":
            # Before the sampling edge the feedback state still represents the previous symbol.
            error = max(abs(t.at("SUM", (i + .4) * ui) - (.6 + .08 * (2 * bits[i] - 1)))
                        for i in range(8, 94))
            t.check("previous-decision cancellation error V", error, 0, .003)
    else:
        raise ValueError("missing checker for " + name)
    for label in t.header[1:]:
        if label in ("CLOCK", "LOCK", "Q", "D0", "D1", "D2", "D3", "ERROR", "REFERENCE"):
            values = [row[t.header.index(label)] for row in t.rows]
            t.check(label + " lower rail V", min(values), -.05, 1.25)
            t.check(label + " upper rail V", max(values), -.05, 1.25)
    return {"status": "pass" if all(item["passed"] for item in t.results) else "fail",
            "case": name, "checks": t.results, "metrics": metrics}


def compare(cpp, va, left_flow="cpp", right_flow="veriloga", destination=None):
    left = json.loads((cpp / "checks.json").read_text())
    right = json.loads((va / "checks.json").read_text())
    if left["status"] != "pass" or right["status"] != "pass":
        raise ValueError("both independent flow checks must pass before comparison")
    a, b = Trace(cpp, left_flow), Trace(va, right_flow)
    destination = destination or cpp.parent
    destination.mkdir(parents=True, exist_ok=True)
    if a.contract != b.contract or a.header != b.header:
        raise ValueError("flows do not describe the same stimulus and observation contract")
    measurements = []
    # A common, fixed grid prevents adaptive sample density from weighting one solver differently.
    for label in a.header[1:]:
        # Internal tank current/amplitude has integration damping; clock timing is compared separately.
        absolute = .04 if label in ("TANK", "QUAD") else .08
        squared = 0
        peak = 0
        count = 0
        for i in range(int(a.contract.get("acquisition_ui", 32)) * 128, int(a.contract["stop_s"] / a.ui * 128) - 1):
            time = i * a.ui / 128
            error = abs(a.at(label, time) - b.at(label, time))
            squared += error * error
            peak = max(peak, error)
            count += 1
        # Short primitive benches use the whole trajectory, excluding the initial ramp discontinuity.
        if not count:
            for i in range(128, int(a.contract["stop_s"] / a.ui * 128) - 1):
                time = i * a.ui / 128
                error = abs(a.at(label, time) - b.at(label, time))
                squared += error * error
                peak = max(peak, error)
                count += 1
        rms = math.sqrt(squared / count)
        tolerance = .1 if label in ("CLOCK", "Q", "D0", "D1", "D2", "D3", "ERROR", "REFERENCE") else .025 if label in ("CONTROL", "LOCK") else .02 if label == "OUT" else .006
        if label in ("TANK", "QUAD"):
            tolerance = absolute
        if label == "CLOCK":
            if a.contract["case"] not in ("vco", "cdr_no_data"):
                offset = 1.5 if a.contract["case"].startswith("link") else 0
                edge_map = lambda trace: {math.floor(edge / trace.ui - offset - trace.contract["phase_ui"]): edge / trace.ui
                                          for edge in trace.active_edges()
                                          if trace.contract["case"] != "cdr_gap" or edge / trace.ui >= 226}
                first, second = edge_map(a), edge_map(b)
                common = first.keys() & second.keys()
                if len(common) < min(len(first), len(second)) - 1:
                    raise ValueError("recovered clock cycles cannot be matched to the same symbol grid")
                difference = max(abs(first[index] - second[index]) for index in common)
                measurements.append(dict(name="matched recovered-clock edge error UI", value=difference,
                                         tolerance=.035, passed=difference <= .035, diagnostic_voltage_rms_V=rms))
            # An autonomous oscillator's absolute phase drifts with finite integration error; compare frequency and plateaus.
        elif label in ("ERROR", "REFERENCE"):
            begin = a.contract["acquisition_ui"]
            count = int(a.contract["stop_s"] / a.ui * 128) - begin * 128 - 1
            mean_error = abs(sum(a.at(label, i * a.ui / 128) - b.at(label, i * a.ui / 128)
                                 for i in range(begin * 128, begin * 128 + count)) / count)
            measurements.append(dict(name=label + " average pulse contribution V", value=mean_error,
                                     tolerance=.015, passed=mean_error <= .015, diagnostic_voltage_rms_V=rms))
        elif label == "LOCK" and a.contract["case"] == "cdr_gap":
            def lock_events(trace):
                column = trace.header.index("LOCK")
                return [(y[column] > x[column],
                         (x[0] + (y[0] - x[0]) * (.6 - x[column]) / (y[column] - x[column])) / trace.ui)
                        for x, y in zip(trace.rows, trace.rows[1:])
                        if x[0] > 80 * trace.ui and (x[column] - .6) * (y[column] - .6) < 0]
            first, second = lock_events(a), lock_events(b)
            if not first or [rise for rise, _ in first] != [rise for rise, _ in second]:
                raise ValueError("lock loss/reacquisition transitions do not match")
            error = max(abs(x[1] - y[1]) for x, y in zip(first, second))
            measurements.append(dict(name="LOCK transition-time error UI", value=error, tolerance=1,
                                     passed=error <= 1, diagnostic_voltage_rms_V=rms))
            grid = list(range(120 * 128, 140 * 128)) + list(range(226 * 128, 254 * 128))
            steady_rms = math.sqrt(sum((a.at(label, i * a.ui / 128) - b.at(label, i * b.ui / 128)) ** 2
                                      for i in grid) / len(grid))
            measurements.append(dict(name="LOCK steady-state RMS V", value=steady_rms,
                                     tolerance=.025, passed=steady_rms <= .025))
        else:
            if label == "OUT" and a.contract["case"] in ("serializer", "mux2", "xor", "dff", "sample_hold"):
                tolerance = .06
            if label == "DIFF": tolerance = .015
            measurements.append(dict(name=label + " RMS V", value=rms, tolerance=tolerance, passed=rms <= tolerance,
                                     peak_error_V=peak))
    for label in left["metrics"].keys() | right["metrics"].keys():
        x, y = left["metrics"][label], right["metrics"][label]
        # Fixed time-window boundaries can include one extra end cycle after a small phase displacement.
        # Absolute period checks and symbol-indexed edge matching reject missing internal cycles.
        tolerance = {"period_ui": .003, "mean_phase_ui": .035, "cycles": 1, "gain": .025,
                     "phase_rad": .02, "vco_period_ui": .015, "pulse_imbalance_V": .005, "fir_error_V": .006}.get(label, 0)
        measurements.append(dict(name=label, value=abs(x - y), tolerance=tolerance, passed=abs(x - y) <= tolerance))
    with (destination / "difference.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["time_s"] + [label + "_cpp_minus_veriloga_V" for label in a.header[1:]])
        for i in range(int(a.contract["acquisition_ui"]) * 128, int(a.contract["stop_s"] / a.ui * 128) - 1):
            time = i * a.ui / 128
            writer.writerow([time] + [a.at(label, time) - b.at(label, time) for label in a.header[1:]])
    report = {"status": "pass" if all(item["passed"] for item in measurements) else "fail",
              "measurements": measurements, "evidence": "finite deterministic vectors and selected electrical models"}
    (destination / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def loading(nominal, loaded):
    checks, ratios = [], {}
    for flow in ("cpp", "veriloga"):
        a, b = Trace(nominal / flow, flow), Trace(loaded / flow, flow)
        for folder in (nominal / flow, loaded / flow):
            if json.loads((folder / "checks.json").read_text())["status"] != "pass":
                raise ValueError("both loading experiments must independently pass")
        if a.contract["bits"] != b.contract["bits"]:
            raise ValueError("loading experiments use different data")
        for label in ("TX", "RX"):
            grid = range(80 * 128, 190 * 128)
            ratio = math.sqrt(sum(b.at(label, i * b.ui / 128) ** 2 for i in grid) /
                              sum(a.at(label, i * a.ui / 128) ** 2 for i in grid))
            ratios[flow, label] = ratio
            checks.append(dict(name=flow + " " + label + " loaded/nominal RMS", value=ratio,
                               low=.8, high=.99, passed=.8 <= ratio <= .99))
    for label in ("TX", "RX"):
        error = abs(ratios["cpp", label] - ratios["veriloga", label])
        checks.append(dict(name=label + " load-response ratio agreement", value=error,
                           tolerance=.005, passed=error <= .005))
    report = {"status": "pass" if all(c["passed"] for c in checks) else "fail", "checks": checks}
    (loaded / "loading.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("flow", choices=("cpp", "veriloga", "compare", "refine", "loading"))
    parser.add_argument("folder", type=Path)
    parser.add_argument("other", nargs="?", type=Path)
    args = parser.parse_args()
    try:
        if args.flow == "compare":
            report = compare(args.folder, args.other)
        elif args.flow == "loading":
            report = loading(args.folder, args.other)
        elif args.flow == "refine":
            flows = {flow: compare(args.folder / flow, args.other / flow, flow, flow,
                                  args.other / "convergence" / flow) for flow in ("cpp", "veriloga")}
            report = {"status": "pass" if all(r["status"] == "pass" for r in flows.values()) else "fail",
                      "flows": flows}
        else:
            trace = Trace(args.folder, args.flow)
            report = evaluate(trace)
            with (args.folder / "trace.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(trace.header)
                writer.writerows(trace.rows)
            (args.folder / "checks.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        return 0 if report["status"] == "pass" else 1
    except (ValueError, KeyError, OSError, ZeroDivisionError) as error:
        print(json.dumps({"status": "error", "error": str(error)}))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
