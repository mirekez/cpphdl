#!/usr/bin/env python3
"""Clocked decisions using the actual SG13G2 HV PSP transistor models."""
import argparse
import bisect
import gzip
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from mos_geometry import size_mos_instances
UI = 8e-9
COUNT = 48


def pwl(points):
    return 'PWL(' + ' '.join(f'{time:.14g} {value:.14g}' for time, value in points) + ')'


def characterize(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    inputs = [SOURCE, SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[3] / 'basic/strongarm_comparator.hpp']
    inputs += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    provenance = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models))
    amplitudes = (.001, .0025, .005, .01, .04, .08)
    differences = [amplitudes[i % 6] * (-1 if (i * 13 + i // 6) % 7 < 3 else 1) for i in range(COUNT)]
    experiments, failures = [], []
    corners = ('mos_tt', 'mos_ss', 'mos_ff') if args.corner == 'all' else (args.corner,)
    for corner, temperature, supply, loaded in itertools.product(
            corners, (-40, 27, 125), (2.97, 3.3, 3.63), (False, True)):
        case = work / f'{corner}_{temperature}_{supply}_{int(loaded)}'
        case.mkdir(exist_ok=True)
        trace = case / 'trace.dat'
        trace.unlink(missing_ok=True)
        command = [(0, 0)]
        clock = [(0, 0)]
        for i, value in enumerate(differences):
            command += [((i + .15) * UI, command[-1][1]), ((i + .19) * UI, value)]
            clock += [((i + .45) * UI, 0), ((i + .475) * UI, supply),
                      ((i + .95) * UI, supply), ((i + .975) * UI, 0)]
        # Same eleven-MOS connections and explicit charges as the C++ IC.
        # The four-terminal PSP devices additionally include body/intrinsic charge.
        deck = f'''SG13G2 clocked comparator, 125 MHz
.lib "{models / 'models/cornerMOShv.lib'}" {corner}
.temp {temperature}
VSUPPLY vdd 0 {supply}
VCM center 0 {supply / 2}
VDIFF diff 0 {pwl(command)}
EPLUS source_p center diff 0 .5
EMINUS source_n center diff 0 -.5
RP source_p plus 50
RN source_n minus 50
VCK clock 0 {pwl(clock)}
XIP left plus tail 0 sg13_hv_nmos w=50u l=.45u
XIN right minus tail 0 sg13_hv_nmos w=50u l=.45u
XT tail clock 0 0 sg13_hv_nmos w=50u l=.45u
XFN on op left 0 sg13_hv_nmos w=20u l=.45u
XFNP op on right 0 sg13_hv_nmos w=20u l=.45u
XFP op on vdd vdd sg13_hv_pmos w=40u l=.45u
XFPN on op vdd vdd sg13_hv_pmos w=40u l=.45u
XRL left clock vdd vdd sg13_hv_pmos w=10u l=.45u
XRR right clock vdd vdd sg13_hv_pmos w=10u l=.45u
XRP op clock vdd vdd sg13_hv_pmos w=10u l=.45u
XRN on clock vdd vdd sg13_hv_pmos w=10u l=.45u
CL left 0 .03p
CR right 0 .03p
CT tail 0 .03p
COP op 0 {'.2p' if loaded else '.05p'}
CON on 0 {'.2p' if loaded else '.05p'}
CGSP plus tail .02p
CGSN minus tail .02p
CGDP plus left .003p
CGDN minus right .003p
.options method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8
.control
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
tran {UI / 1024:.14g} {COUNT * UI:.14g}
let differential = v(plus) - v(minus)
let supply_current = -i(VSUPPLY)
wrdata trace.dat v(op) v(on) v(left) v(right) differential supply_current
quit
.endc
.end
'''
        deck, geometry = size_mos_instances(deck)
        (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (case / 'bench.cir').write_text(deck)
        process = subprocess.run([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case,
                                 env=environment, capture_output=True, text=True, timeout=90)
        log = process.stdout + process.stderr
        (case / 'ngspice.log').write_text(log)
        if process.returncode or re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I):
            raise ValueError('PSP comparator simulation failed: ' + str(case / 'ngspice.log'))
        with trace.open() as file:
            next(file)
            data = [[float(x) for x in line.split()] for line in file if line.strip()]
        times = [row[0] for row in data]
        if (len(data) < COUNT * 256 or abs(times[-1] / (COUNT * UI) - 1) > 1e-6 or
                any(len(row) != 7 or not all(math.isfinite(x) for x in row) for row in data)):
            raise ValueError('incomplete/nonfinite comparator waveform')

        def at(time, column):
            index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
            first, second = data[index - 1], data[index]
            fraction = (time - first[0]) / (second[0] - first[0])
            return first[column] + fraction * (second[column] - first[column])

        errors = 0
        rail_error = reset_error = 0.
        for i in range(3, COUNT - 1):
            p, n = (at((i + .8) * UI, column) for column in (1, 2))
            positive = differences[i] > 0
            errors += int((p > n) != positive)
            rail_error = max(rail_error, abs(p - (supply if positive else 0)), abs(n - (0 if positive else supply)))
            reset_error = max(reset_error, *(abs(at((i + .35) * UI, column) - supply) for column in (1, 2, 3, 4)))
        energy = sum((second[0] - first[0]) * supply * (first[6] + second[6]) / 2
                     for first, second in zip(data, data[1:]) if first[0] >= 3 * UI)
        power = energy / ((COUNT - 3) * UI)
        checks = dict(decisions=errors == 0, rails=rail_error < .1,
                      reset=reset_error < .1, supply_power=0 < power < .02)
        compressed = trace.with_suffix('.dat.gz')
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        trace.unlink()
        artifacts = (case / 'bench.cir', case / 'ngspice.log', compressed)
        experiments.append(dict(corner=corner, temperature_C=temperature, supply_V=supply,
                                output_load_F=.2e-12 if loaded else .05e-12,
                                decision_errors=errors, max_rail_error_V=rail_error,
                                max_precharge_error_V=reset_error, mean_supply_power_W=power, checks=checks,
                                artifact_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts}))
        failures += [case.name + ': ' + name for name, passed in checks.items() if not passed]
        print(case.name + ': ' + ('pass' if all(checks.values()) else 'fail'), flush=True)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in provenance.items()):
        raise ValueError('comparator source/model changed during characterization')
    report = dict(status='fail' if failures else 'pass', cases=len(experiments),
                  scope='PSP transient PVT characterization; no mismatch/noise/layout qualification or ADC claim',
                  same_equations_as_square_law=False, input_source_ohm=50,
                  clock_Hz=125e6, input_differences_V=list(amplitudes),
                  experiments=experiments, failures=failures,
                  source_sha256=provenance)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    if failures:
        raise ValueError('\n'.join(failures))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--corner', choices=('all', 'mos_tt', 'mos_ss', 'mos_ff'), default='all')
    characterize(parser.parse_args())
