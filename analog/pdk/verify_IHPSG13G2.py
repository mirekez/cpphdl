#!/usr/bin/env python3
"""Check IHP model loading, an actual PDK CMOS inverter, and KLayout Python setup."""

import argparse
import math
import os
from pathlib import Path
import re
import subprocess


def verify(root, ngspice, output, skip_layout_python=False):
    technology = root / "ihp-sg13g2"
    output.mkdir(parents=True, exist_ok=True)
    if not skip_layout_python:
        import klayout.db as db

        tech = db.Technology()
        tech.load(str(technology / "libs.tech/klayout/tech/sg13g2.lyt"))
        if tech.name != "sg13g2":
            raise ValueError("Unexpected KLayout technology name: " + tech.name)
        layout = db.Layout()
        layout.dbu = 0.001
        cell = layout.create_cell("PDK_SETUP_CHECK")
        cell.shapes(layout.layer(8, 0)).insert(db.Box(0, 0, 1000, 1000))
        layout.write(str(output / "layout_io.gds"))
        reread = db.Layout()
        reread.read(str(output / "layout_io.gds"))
        if reread.top_cell().name != "PDK_SETUP_CHECK":
            raise ValueError("KLayout GDSII round-trip failed")
        print("KLayout Python technology load and GDSII round-trip passed (not a DRC test)")

    model_entry = technology / "libs.tech/ngspice/models/cornerMOSlv.lib"
    deck = output / "inverter.cir"
    trace = output / "inverter.dat"
    # X instances use the PDK subcircuits, including their PSP compact models.
    deck.write_text(f"""IHP SG13G2 PDK setup smoke test, LV CMOS inverter
.lib "{model_entry}" mos_tt
Vdd vdd 0 1.2
Vin in 0 0
Xn out in 0 0 sg13_lv_nmos w=1u l=0.13u
Xp out in vdd vdd sg13_lv_pmos w=2u l=0.13u
.control
set wr_singlescale
set wr_vecnames
dc Vin 0 1.2 0.1
wrdata inverter.dat v(out)
quit
.endc
.end
""")
    environment = dict(os.environ, PDK_ROOT=str(root), PDK="ihp-sg13g2",
                       PDKPATH=str(technology),
                       SPICE_USERINIT_DIR=str(technology / "libs.tech/ngspice"))
    trace.unlink(missing_ok=True)
    completed = subprocess.run([str(ngspice), "-b", str(deck)], cwd=output,
                               env=environment, text=True, capture_output=True, timeout=120)
    log = completed.stdout + completed.stderr
    (output / "ngspice.log").write_text(log)
    if completed.returncode or re.search(r"\b(error|fatal|unknown|failed)\b|not found", log, re.IGNORECASE):
        raise ValueError("IHP ngspice smoke test failed; inspect " + str(output / "ngspice.log"))
    if not trace.is_file():
        raise ValueError("ngspice produced no inverter trace; inspect " + str(output / "ngspice.log"))
    samples = []
    for line in trace.read_text().splitlines():
        try:
            values = [float(value) for value in line.split()]
        except ValueError:
            continue
        if len(values) != 2 or not all(math.isfinite(value) for value in values):
            raise ValueError("Unexpected ngspice output columns or nonfinite values")
        samples.append(values)
    if len(samples) != 13:
        raise ValueError("Expected 13 DC sweep points, got " + str(len(samples)))
    if abs(samples[0][0]) > 1e-8 or abs(samples[-1][0] - 1.2) > 1e-8:
        raise ValueError("DC sweep does not cover the configured input range")
    if not (1.1 < samples[0][1] < 1.3 and -0.1 < samples[-1][1] < 0.1):
        raise ValueError("PDK inverter did not switch between its supply rails")
    if any(right[1] > left[1] + 1e-6 for left, right in zip(samples, samples[1:])):
        raise ValueError("PDK inverter transfer curve is not monotonic")
    print(f"IHP MOS inverter passed: Vout(0 V)={samples[0][1]:.6g} V, "
          f"Vout(1.2 V)={samples[-1][1]:.6g} V")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--ngspice", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-layout-python", action="store_true")
    args = parser.parse_args()
    try:
        verify(args.root.resolve(), args.ngspice.resolve(), args.output.resolve(), args.skip_layout_python)
    except (ValueError, OSError, ImportError, subprocess.TimeoutExpired) as error:
        parser.exit(1, "IHP verification: " + str(error) + "\n")


if __name__ == "__main__":
    main()
