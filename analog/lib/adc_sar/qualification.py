"""Process-independent acceptance gates for measured one-slice ADC evidence.

Limits use SI units (DNL/INL in LSB). A missing limit stays UNSPECIFIED;
measurement evidence alone never becomes a product acceptance claim.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path


LIMITS = {
    "dnl": ("maximum_abs_dnl_lsb", "maximum"),
    "inl": ("maximum_abs_inl_lsb", "maximum"),
    "average_power": ("maximum_full_cycle_rail_power_W", "maximum"),
    "peak_current": ("maximum_peak_rail_current_A", "maximum"),
    "sndr": ("minimum_sndr_db", "minimum"),
    "enob": ("minimum_enob_bits", "minimum"),
    "sfdr": ("minimum_sfdr_db", "minimum"),
    "absolute_hold": ("maximum_absolute_hold_V", "maximum"),
}
REQUIRED_CHARACTERIZATION = ("frequency", "amplitude", "impedance", "kickback",
                             "psrr", "acquisition", "evaluation", "jitter", "power")


def current_source_sha256(repository):
    """Use the same source inventory as the library campaign runner."""
    root = repository / "analog/lib/adc_sar"
    paths = set()
    for folder in (root, repository / "analog/include", repository / "include",
                   repository / "analog/tests"):
        for pattern in ("*.h", "*.hpp", "*.cpp", "*.py", "CMakeLists.txt"):
            paths.update(folder.rglob(pattern))
    return {str(path.relative_to(repository)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(paths)}


def require_current_source(campaign, repository):
    recorded = campaign.get("source_sha256")
    if not isinstance(recorded, dict) or not recorded:
        raise ValueError("campaign has no source lock")
    current = current_source_sha256(repository)
    if current != recorded:
        changed = sorted(k for k in set(current) | set(recorded) if current.get(k) != recorded.get(k))
        raise ValueError(f"campaign source does not match current source ({len(changed)} changed files)")


def finite(value, label, *, positive=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} must be finite")
    if positive and value <= 0:
        raise ValueError(f"{label} must be positive")
    return float(value)


def linearity_evidence(record):
    if record.get("status") != "PASS" or record.get("smoke") or not all(k in record for k in ("up", "down")):
        raise ValueError("complete two-direction electrical transition evidence is required")
    directions = [record[k] for k in ("up", "down")]
    if directions[0]["complete_codes"] != directions[1]["complete_codes"] or not directions[0]["complete_codes"]:
        raise ValueError("transition directions have different complete-code coverage")
    if any(d["missing_complete_codes"] for d in directions):
        raise ValueError("missing code in transition evidence")
    codes = directions[0]["complete_codes"]
    if any(b != a + 1 for a, b in zip(codes, codes[1:])):
        raise ValueError("complete-code coverage is not contiguous")
    for direction in directions:
        for key, expected in (("dnl_bounds_lsb", len(codes)),
                              ("inl_bounds_lsb", len(codes) + 1)):
            if len(direction[key]) != expected:
                raise ValueError(f"{key} does not cover every complete code")
            for pair in direction[key]:
                if len(pair) != 2 or finite(pair[0], key) > finite(pair[1], key):
                    raise ValueError(f"invalid {key} interval")
    def worst(key):
        values = [finite(x, key) for d in directions for pair in d[key] for x in pair]
        if not values:
            raise ValueError(f"empty {key}")
        return max(map(abs, values))
    return dict(maximum_abs_dnl_lsb=worst("dnl_bounds_lsb"),
                maximum_abs_inl_lsb=worst("inl_bounds_lsb"),
                complete_code_count=len(directions[0]["complete_codes"]),
                complete_code_range=[directions[0]["complete_codes"][0], directions[0]["complete_codes"][-1]],
                linearity_scope="endpoint-fitted, bracket-conservative, complete bins in the measured input range")


def power_evidence(characterization):
    if set(REQUIRED_CHARACTERIZATION) - set(characterization):
        raise ValueError("missing required characterization suites for rail power")
    record = characterization["power"]
    refinement = record.get("power_refinement")
    if record.get("status") != "PASS" or record.get("suite") != "power" or not refinement:
        raise ValueError("complete refined full-cycle power evidence is required")
    if refinement.get("method") != "BDF2" or len(refinement.get("steps_ps", [])) != 2:
        raise ValueError("power reference must contain two BDF2 resolutions")
    differences = refinement.get("relative_differences", {})
    for key, value in differences.items():
        tolerance = refinement["peak_limit"] if key.endswith("peak_A") else refinement["energy_charge_limit"]
        if finite(value, key) > finite(tolerance, key + " tolerance", positive=True):
            raise ValueError("power refinement tolerance failed")
    if not differences:
        raise ValueError("missing power refinement differences")
    watts = []
    peaks = []
    cases = 0
    for suite in REQUIRED_CHARACTERIZATION:
        summary = characterization[suite]
        if summary.get("status") != "PASS" or summary.get("suite") != suite:
            raise ValueError(f"{suite} characterization did not pass")
        required = [case for case in summary.get("cases", []) if case.get("required")]
        if not required:
            raise ValueError(f"no required {suite} operating points")
        cases += len(required)
        for case in required:
            if case.get("failed_operating_points"):
                raise ValueError(f"required {suite} case contains failed conversions")
            n = case["conversions"]
            fields = ("full_cycle_io_energy_J", "full_cycle_core_energy_J",
                      "full_cycle_io_peak_A", "full_cycle_core_peak_A")
            if not isinstance(n, int) or n < 1 or any(len(case.get(key, [])) != n for key in fields):
                raise ValueError("incomplete full-cycle rail records")
            period = finite(case["configuration"]["sample_period_s"], "sample period", positive=True)
            for io, core, io_peak, core_peak in zip(*(case[key] for key in fields)):
                io = finite(io, "IO cycle energy")
                core = finite(core, "core cycle energy")
                io_peak = finite(io_peak, "IO peak current")
                core_peak = finite(core_peak, "core peak current")
                if min(io, core, io_peak, core_peak) < 0:
                    raise ValueError("negative delivered energy or peak current")
                watts.append((io + core) / period)
                peaks.append(max(io_peak, core_peak))
    return dict(maximum_full_cycle_rail_power_W=max(watts),
                maximum_peak_rail_current_A=max(peaks),
                required_cycles=len(watts), required_power_cases=cases,
                power_scope="all required characterization cycles; IO plus core rail energy; peak is largest individual rail")


def spectral_evidence(record):
    if record.get("status") != "PASS" or record.get("suite") != "frequency":
        raise ValueError("coherent frequency characterization is required")
    required = [case for case in record.get("cases", []) if case.get("required")]
    if not required:
        raise ValueError("no required spectral operating points")
    rows = []
    for case in required:
        if case.get("failed_operating_points") or not case.get("spectrum"):
            raise ValueError("failed or missing required spectral record")
        rows.append(case["spectrum"])
    return dict(minimum_sndr_db=min(finite(row["sndr_db"], "SNDR") for row in rows),
                minimum_enob_bits=min(finite(row["enob_at_stimulus"], "ENOB") for row in rows),
                minimum_sfdr_db=min(finite(row["sfdr_db"], "SFDR") for row in rows),
                required_records=len(rows),
                spectral_scope="coherent code spectra at the declared stimulus amplitudes and frequencies")


def hold_evidence(conversions):
    # Baseline and specified input range only. Deliberate negative controls and
    # separate stress experiments cannot silently alter this rated metric.
    rated = [row for row in conversions if row.get("experiment") in ("conversion", "range")]
    if not rated or {row["experiment"] for row in rated} != {"conversion", "range"}:
        raise ValueError("baseline and input-range hold records are required")
    if any(row.get("pass") not in ("1", 1, True) for row in rated):
        raise ValueError("a rated hold conversion failed")
    return dict(maximum_absolute_hold_V=max(finite(float(row["absolute_hold_V"]), "absolute hold")
                                            for row in rated), rated_conversions=len(rated),
                hold_scope="baseline and specified input-range conversions")


def evaluate(measurements, limits):
    if set(limits) - set(LIMITS):
        raise ValueError("unknown ADC acceptance limits: " + ", ".join(sorted(set(limits)-set(LIMITS))))
    checks = {}
    for name, (measurement, direction) in LIMITS.items():
        if measurement not in measurements:
            raise ValueError(f"missing measured {measurement}")
        value = finite(measurements[measurement], measurement)
        limit = limits.get(name)
        if limit is None:
            checks[name] = dict(status="UNSPECIFIED", measured=value, limit=None, direction=direction)
            continue
        limit = finite(limit, f"{name} limit", positive=name not in ("sndr", "enob", "sfdr"))
        passed = value <= limit if direction == "maximum" else value >= limit
        checks[name] = dict(status="PASS" if passed else "FAIL", measured=value,
                            limit=limit, direction=direction)
    return dict(status="FAIL" if any(x["status"] == "FAIL" for x in checks.values()) else
                "PASS" if all(x["status"] == "PASS" for x in checks.values()) else "INCOMPLETE",
                checks=checks, measurements=measurements,
                scope="generic measured-evidence budget gate; process/layout qualification is separate")


def from_campaign(campaign, limits):
    if campaign.get("status") != "PASS":
        raise ValueError("a passing source-locked library campaign is required")
    needed = {"transitions", "conversion", "range"} | {"characterize_" + name for name in REQUIRED_CHARACTERIZATION}
    if needed.intersection(campaign.get("omitted_suites", [])):
        raise ValueError("a required acceptance evidence suite was omitted")
    suites = {row["name"].removeprefix("analog.lib.adc_sar."): row["status"]
              for row in campaign.get("suites", [])}
    if suites and any(suites.get(name) != "PASS" for name in needed):
        raise ValueError("a required acceptance evidence suite did not pass")
    transitions = campaign.get("transitions")
    characterization = campaign.get("characterization", {})
    if not transitions or not all(k in characterization for k in REQUIRED_CHARACTERIZATION):
        raise ValueError("campaign lacks linearity, power or frequency evidence")
    measurements = {}
    for extractor, source in ((linearity_evidence, transitions),
                              (power_evidence, characterization),
                              (spectral_evidence, characterization["frequency"]),
                              (hold_evidence, campaign.get("conversions", []))):
        measurements.update(extractor(source))
    return evaluate(measurements, limits)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", type=Path, required=True, help="complete campaign checks.json")
    parser.add_argument("--limits", type=Path, required=True, help="JSON object; null means unspecified")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    campaign = json.loads(args.campaign.read_text())
    try:
        require_current_source(campaign, Path(__file__).resolve().parents[3])
        result = from_campaign(campaign, json.loads(args.limits.read_text()))
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(result["status"], args.output)
    return result["status"] == "FAIL"


if __name__ == "__main__":
    raise SystemExit(main())
