#!/usr/bin/env python3
"""Regenerate the checked-in Verilog-A catalog from native C++ elaboration."""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess
import tempfile


EXAMPLES = {
    "agc": ("nominal", "weak", "refined"),
    "class_variants": ("nominal",),
    "cmos_inverter": ("nominal",),
    "ldo": ("nominal", "dropout", "refined"),
    "mutual_influence": ("nominal",),
    "opamp_feedback": ("nominal",),
    "pll": ("nominal", "loaded", "refined"),
    "rc_filter": ("nominal",),
    "regulator": ("nominal",),
    "transistor_pair": ("nominal",),
    "two_transistor_expression": ("nominal",),
}
LIMITS = {
    "digital_pipeline": "This example contains only managed bit arithmetic and clocked registers. "
    "It has no analog circuit to export. Verilog-A export rejects its digital operations; "
    "use a digital HDL backend for the pipeline. No empty or invented analog module is supplied.",
    "diagnostics": "This example deliberately constructs invalid parameters, a floating network, "
    "multiple bit writers, unknown physical drive and a violated assumption. It is a diagnostic "
    "test suite, not a valid IC model. Its intended result is the native diagnostic report; "
    "no Verilog-A model can represent the framework's complete diagnostics contract.",
    "mixed_signal": "MixedReceiverAnalog.va contains the actual elaborated RC input, finite-resistance "
    "output driver, output capacitance and load. It is an analog partition, not the complete receiver. "
    "The host must supply Vdd = 1.2 V relative to Vss, ground Vss, and drive VIN as in the "
    "original bench (0 V, then 1.2 V at 750 ns). The cpphdl_bit<ID> port is SECOND, driven at "
    "0/1 V relative to host ground. The native two-register pipeline, clock and threshold sensing "
    "remain external. IN and Y are accessible electrical nodes. Never leave the bit control floating.",
}


def run(arguments):
    result = subprocess.run([str(value) for value in arguments], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{arguments[0]} failed ({result.returncode}):\n{result.stdout}{result.stderr}")


def generated_files(root):
    return {path.relative_to(root): path.read_bytes()
            for folder in (root / "basic/generated", root / "examples/generated")
            if folder.exists() for path in folder.rglob("*")
            if path.is_file() and (path.suffix in {".va", ".cir"} or
                                   path.name in {"columns.csv", "conditions.json"})}


def relative_location(match, root):
    source = Path(match[1])
    if source.is_absolute():
        # Resolve workspace symlinks as well as include paths containing ../.
        try:
            source = source.resolve().relative_to(root)
        except ValueError:
            raise RuntimeError(f"generated model depends on a source outside the package: {source}")
    else:
        source = Path(os.path.normpath(source))
    return f", {source.as_posix()}:{match[2]}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path, help="cpphdl package root")
    parser.add_argument("--models", required=True, type=Path, help="native model catalog executable")
    parser.add_argument("--example", action="append", default=[], metavar="NAME=EXECUTABLE")
    parser.add_argument("--check", action="store_true", help="compare against tracked files without changing them")
    parser.add_argument("--openvaf", type=Path, help="also compile every generated .va into temporary OSDI files")
    args = parser.parse_args()
    root = args.root.resolve()
    analog = root / "analog"
    executables = dict(value.split("=", 1) for value in args.example)
    actual = {path.parent.name for path in (analog / "examples").glob("*/main.cpp")}
    if actual != EXAMPLES.keys() | LIMITS.keys():
        raise RuntimeError("example catalog is incomplete; update EXAMPLES/LIMITS for the current source folders")
    if not (EXAMPLES.keys() | {"mixed_signal"}) <= executables.keys():
        raise RuntimeError("pass --example NAME=EXECUTABLE for every exportable example")
    with tempfile.TemporaryDirectory(prefix="cpphdl-veriloga-catalog-") as directory:
        output = Path(directory)
        run([args.models, output])
        for name, scenarios in EXAMPLES.items():
            for scenario in scenarios:
                destination = output / "examples/generated" / name
                if scenario != "nominal":
                    destination /= scenario
                run([executables[name], destination, scenario, "--verilog-a"])
                # Module names are unique when examples are compiled into one simulator library.
                module = "Example_" + name + "_" + scenario
                for filename in ("design.va", "bench.cir"):
                    path = destination / filename
                    path.write_text(path.read_text().replace("AnalogExperiment", module))
        destination = output / "examples/generated/mixed_signal"
        run([executables["mixed_signal"], destination, "--verilog-a"])
        for path in output.rglob("*.va"):
            text = re.sub(r", ([^,\n]+):(\d+)(?=\n)", lambda match: relative_location(match, root), path.read_text())
            path.write_text(text)
        expected = generated_files(output)
        if args.openvaf:
            compiled = output / "compiled"
            compiled.mkdir()
            for index, path in enumerate(sorted(output.rglob("*.va"))):
                run([args.openvaf, path, "-o", compiled / f"model_{index}.osdi"])
        current = generated_files(analog)
        if args.check:
            differences = sorted(path for path in current.keys() | expected.keys()
                                 if current.get(path) != expected.get(path))
            for path in differences:
                print(f"stale or missing generated file: {path}")
                if path in current and path in expected:
                    print("".join(difflib.unified_diff(current[path].decode().splitlines(True),
                                                    expected[path].decode().splitlines(True)))[:3000])
            if differences:
                return 1
        else:
            for path in current.keys() - expected.keys():
                (analog / path).unlink()
            for path, contents in expected.items():
                target = analog / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(contents)
            for name in actual:
                target = analog / "examples/generated" / name / "README.md"
                target.parent.mkdir(parents=True, exist_ok=True)
                text = f"# {name}\n\n[Original C++ model and explanation](../../{name}/README.md).\n\n"
                if name in LIMITS:
                    text += LIMITS[name] + "\n"
                else:
                    text += ("`design.va` is the complete elaborated analog bench, including loads and "
                             "identified simulation models. `bench.cir` supplies sources, initial capacitor "
                             "voltages and transient analysis. `columns.csv` names voltage observations; "
                             "`conditions.json` records timing and initialization. These are generated inputs, "
                             "not waveform results or a claim that native purpose constraints passed.\n\n"
                             "Run `openvaf design.va -o design.osdi`, then `ngspice -n -b bench.cir` here. "
                             "This writes simulation results; keep those files out of Git.\n")
                    if len(EXAMPLES[name]) > 1:
                        text += "\nThe nominal bench is here; " + ", ".join(
                            f"`{scenario}/`" for scenario in EXAMPLES[name][1:]) + " hold the other native scenarios.\n"
                    if name == "mutual_influence":
                        text += "\nThe original native bench intentionally fails its output purposes after an external driver interferes.\n"
                    if name == "ldo":
                        text += ("\nThese files preserve the example's original timing. Its coarse native startup is numerically "
                                 "damped; use the finer dedicated cross-simulator regression to assess solver agreement.\n")
                target.write_text(text)
        count = sum(path.suffix == ".va" for path in expected)
        print(f"{'Verified' if args.check else 'Generated'} {count} Verilog-A files; "
              "digital_pipeline and diagnostics have documented export limits.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
