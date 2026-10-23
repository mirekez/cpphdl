#!/usr/bin/env python3
"""Loaded CMOS sampling with actual SG13G2 HV PSP devices, including injection."""
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
COUNT = 16


def characterize(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    inputs = [SOURCE, SOURCE.parents[1] / 'mos_geometry.py', SOURCE.parents[3] / 'basic/mos_track_hold.hpp']
    inputs += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    provenance = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models))
    experiments, failures = [], []
    corners = ('mos_tt', 'mos_ss', 'mos_ff') if args.corner == 'all' else (args.corner,)
    for corner, temperature, supply, loaded in itertools.product(
            corners, (-40, 27, 125), (2.97, 3.3, 3.63), (False, True)):
        case = work / f'{corner}_{temperature}_{supply}_{int(loaded)}'
        case.mkdir(exist_ok=True)
        trace = case / 'trace.dat'
        trace.unlink(missing_ok=True)
        command, clock = [(0, supply / 2)], [(0, 0)]
        for i in range(COUNT):
            command += [((i + .15) * UI, command[-1][1]),
                        ((i + .19) * UI, supply / 2 + .25 * ((i * 3) % 5 - 2)),
                        ((i + .65) * UI, supply / 2 + .25 * ((i * 3) % 5 - 2)),
                        ((i + .69) * UI, supply / 2 + .5 * ((i * 7) % 5 - 2))]
            clock += [((i + .02) * UI, 0), ((i + .045) * UI, supply),
                      ((i + .52) * UI, supply), ((i + .545) * UI, 0)]
        pwl = lambda points: 'PWL(' + ' '.join(f'{t:.14g} {v:.14g}' for t, v in points) + ')'
        deck = f'''SG13G2 CMOS sample/hold, 125 MHz
.lib "{models / 'models/cornerMOShv.lib'}" {corner}
.temp {temperature}
VSUPPLY vdd 0 {supply}
VSOURCE source 0 {pwl(command)}
RINPUT source input 50
VCK clock 0 {pwl(clock)}
XIN nclock clock 0 0 sg13_hv_nmos w=10u l=.45u
XIP nclock clock vdd vdd sg13_hv_pmos w=20u l=.45u
XSN output clock input 0 sg13_hv_nmos w=50u l=.45u
XSP output nclock input vdd sg13_hv_pmos w=100u l=.45u
XDN output nclock output 0 sg13_hv_nmos w=25u l=.45u
XDP output clock output vdd sg13_hv_pmos w=50u l=.45u
CH output 0 2p
CC nclock 0 .005p
CGN clock output .002p
CGP nclock output .002p
COFF input output .001p
RLEAK output 0 1e9
''' + ('RLOAD output 0 200000\n' if loaded else '') + f'''.options method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8
.control
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
tran {UI / 512:.14g} {COUNT * UI:.14g}
wrdata trace.dat v(output) v(input) v(source)
quit
.endc
.end
'''
        deck, geometry = size_mos_instances(deck)
        (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (case / 'bench.cir').write_text(deck)
        process = subprocess.run([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case,
                                 env=environment, capture_output=True, text=True, timeout=60)
        log = process.stdout + process.stderr
        (case / 'ngspice.log').write_text(log)
        if process.returncode or re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I):
            raise ValueError('PSP sampler simulation failed: ' + str(case / 'ngspice.log'))
        with trace.open() as file:
            next(file)
            data = [[float(x) for x in line.split()] for line in file if line.strip()]
        if (len(data) < COUNT * 256 or abs(data[-1][0] / (COUNT * UI) - 1) > 1e-6 or
                any(len(row) != 4 or not all(math.isfinite(x) for x in row) for row in data)):
            raise ValueError('incomplete/nonfinite sampler waveform')
        times = [row[0] for row in data]

        def at(time, column):
            index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
            first, second = data[index - 1], data[index]
            return first[column] + (second[column] - first[column]) * (time - first[0]) / (second[0] - first[0])

        acquisition = hold = injection = loading = 0.
        resistance = 1 / (1e-9 + (1 / 200000 if loaded else 0))
        for i in range(3, COUNT - 1):
            target = supply / 2 + .25 * ((i * 3) % 5 - 2)
            acquisition = max(acquisition, abs(at((i + .45) * UI, 1) - target))
            held = at((i + .60) * UI, 1)
            injection = max(injection, abs(held - at((i + .50) * UI, 1)))
            expected = held * math.exp(-.30 * UI / (resistance * 2.005e-12))
            hold = max(hold, abs(at((i + .90) * UI, 1) - expected))
            loading = max(loading, *(abs(at((i + phase) * UI, 2) - at((i + phase) * UI, 3)) for phase in (.20, .24, .28)))
        # These are front-end building-block limits, not an eight-bit ADC claim.
        checks = dict(acquisition=acquisition < .01, hold_feedthrough=hold < .015,
                      turnoff_injection=injection < .03, finite_input_loading=.005 < loading < 1.)
        compressed = trace.with_suffix('.dat.gz')
        with trace.open('rb') as original, gzip.open(compressed, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(original, output)
        trace.unlink()
        experiments.append(dict(corner=corner, temperature_C=temperature, supply_V=supply,
                                loaded=loaded, acquisition_error_V=acquisition, hold_feedthrough_V=hold,
                                turnoff_injection_V=injection, input_loading_V=loading, checks=checks,
                                artifact_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                                 for p in (case / 'bench.cir', case / 'ngspice.log', compressed)}))
        failures += [case.name + ': ' + name for name, passed in checks.items() if not passed]
        print(case.name + ': ' + ('pass' if all(checks.values()) else 'fail'), flush=True)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in provenance.items()):
        raise ValueError('sampler source/model changed during characterization')
    report_path.write_text(json.dumps(dict(status='fail' if failures else 'pass', cases=len(experiments),
        scope='PSP sample/hold PVT; no mismatch, noise, layout or ADC resolution claim',
        same_equations_as_square_law=False, experiments=experiments, failures=failures,
        source_sha256=provenance), indent=2) + '\n')
    if failures:
        raise ValueError('\n'.join(failures))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--corner', choices=('all', 'mos_tt', 'mos_ss', 'mos_ff'), default='all')
    characterize(parser.parse_args())
