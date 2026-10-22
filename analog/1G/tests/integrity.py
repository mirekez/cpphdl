#!/usr/bin/env python3
"""Exercise failure detection using deliberately damaged simulation evidence."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import tempfile

from verify import Trace, compare, evaluate


def rejects(action, message):
    try:
        action()
    except ValueError:
        return
    raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, help="passing DAC C++ artifact directory")
    parser.add_argument("--veriloga", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cpphdl-1g-integrity-") as temporary:
        work = Path(temporary)
        for name in ("contract.json", "conditions.json", "columns.csv"):
            shutil.copyfile(args.folder / name, work / name)
        (work / "cpp").mkdir()
        with (args.folder / "cpp/trace.csv").open(newline="") as file:
            original = list(csv.reader(file))

        def write(rows):
            with (work / "cpp/trace.csv").open("w", newline="") as file:
                csv.writer(file).writerows(rows)

        write(original)
        assert evaluate(Trace(work, "cpp"), None)["status"] == "pass"
        write(original[:-10])
        rejects(lambda: Trace(work, "cpp"), "truncated native output accepted")
        damaged = [row[:] for row in original]
        damaged[20][1] = "nan"
        write(damaged)
        rejects(lambda: Trace(work, "cpp"), "nonfinite native sample accepted")
        damaged = [row[:] for row in original]
        damaged[20][0] = damaged[19][0]
        write(damaged)
        rejects(lambda: Trace(work, "cpp"), "duplicate time accepted")
        damaged = [row[:] for row in original]
        for row in damaged[1:]:
            row[1] = str(float(row[1]) + .05)
        write(damaged)
        assert evaluate(Trace(work, "cpp"), None)["status"] == "fail", "incorrect levels accepted"
        (work / "checks.json").write_text(json.dumps(dict(status="fail")))
        rejects(lambda: compare(work, args.folder), "comparison accepted a failed absolute check")

        if args.veriloga:
            lines = (args.veriloga / "spice.dat").read_text().splitlines()
            (work / "spice.dat").write_text("\n".join(lines[:-10]) + "\n")
            rejects(lambda: Trace(work, "veriloga"), "truncated SPICE output accepted")
    print("Checker rejected truncated output, nonfinite samples, duplicate times, wrong levels and failed-flow comparison")


if __name__ == "__main__":
    main()
