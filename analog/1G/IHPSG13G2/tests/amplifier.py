#!/usr/bin/env python3
"""Characterize the resistor-loaded receive pair using actual IHP HV PSP MOS.

This is an independent process-model experiment, not a claim that the generic
square-law model has the same equations or that the circuit has a verified layout.
"""
import argparse
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from mos_geometry import size_mos_instances


def rows(path):
    with path.open() as file:
        header = next(file).split()
        result = [[float(x) for x in line.split()] for line in file if line.strip()]
    if not result or any(len(row) != len(header) or not all(math.isfinite(x) for x in row) for row in result):
        raise ValueError('missing/nonfinite PSP observations in ' + str(path))
    return result


def interpolate(data, frequency):
    for first, second in zip(data, data[1:]):
        if first[0] <= frequency <= second[0]:
            fraction = (frequency - first[0]) / (second[0] - first[0])
            return first[1] + fraction * (second[1] - first[1])
    raise ValueError('AC observations do not cover the requested frequency')


def characterize(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    technology = args.pdk.resolve() / 'ihp-sg13g2'
    models = technology / 'libs.tech/ngspice'
    inputs = [SOURCE, SOURCE.parents[1] / 'mos_geometry.py',
              SOURCE.parents[3] / 'basic/nmos_resistor_amplifier.hpp', args.ngspice.resolve()]
    inputs += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    provenance = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models))
    experiments = []
    failures = []
    for corner, temperature, supply, loaded in itertools.product(
            ('mos_tt', 'mos_ss', 'mos_ff'), (-40, 27, 125), (2.97, 3.3, 3.63), (False, True)):
        name = f'{corner}_{temperature}_{supply}_{int(loaded)}'
        case = work / name
        case.mkdir(exist_ok=True)
        for stale in ('dc.dat', 'ac.dat', 'noise.dat', 'noise_total.dat'):
            (case / stale).unlink(missing_ok=True)
        deck = f'''IHP SG13G2 resistor-loaded receive pair, W=50um L=0.45um
.lib "{models / 'models/cornerMOShv.lib'}" {corner}
.temp {temperature}
VSUPPLY vdd 0 {supply}
VCM center 0 {supply / 2}
VDIFF diff 0 0 ac 1
EGP gp center diff 0 .5
EGN gn center diff 0 -.5
RLP vdd op 1000
RLN vdd on 1000
XP on gp sp 0 sg13_hv_nmos w=50u l=.45u
XN op gn sn 0 sg13_hv_nmos w=50u l=.45u
RDP sp tail 50
RDN sn tail 50
RT tail 0 150
COP op vdd .3p
CON on vdd .3p
CT tail 0 .04p
CGSP gp sp .15p
CGSN gn sn .15p
CGDP gp on .02p
CGDN gn op .02p
''' + ('RLOAD op on 2000\n' if loaded else '') + '''.options reltol=1e-6 abstol=1e-12 vntol=1e-8
.control
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
dc VDIFF -.08 .08 .01
let response = v(op) - v(on)
let supply_current = -i(VSUPPLY)
wrdata dc.dat response v(op) v(on) v(sp) v(sn) v(tail) supply_current
reset
ac dec 30 1meg 1g
let response = v(op) - v(on)
let gain = mag(response)
wrdata ac.dat gain
unset sqrnoise
noise v(op,on) VDIFF dec 30 1k 100meg
setplot noise1
wrdata noise.dat onoise_spectrum inoise_spectrum
setplot noise2
wrdata noise_total.dat onoise_total inoise_total
quit
.endc
.end
'''
        deck, geometry = size_mos_instances(deck)
        (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (case / 'bench.cir').write_text(deck)
        process = subprocess.run([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case,
                                 env=environment, capture_output=True, text=True, timeout=30)
        log = process.stdout + process.stderr
        (case / 'ngspice.log').write_text(log)
        if process.returncode or re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I):
            raise ValueError('PSP experiment failed: ' + str(case / 'ngspice.log'))
        dc, ac = rows(case / 'dc.dat'), rows(case / 'ac.dat')
        if len(dc) != 17 or len(ac) < 80:
            raise ValueError('truncated process characterization')
        gain = (dc[9][1] - dc[7][1]) / .02
        linear_error = max(abs(row[1] - gain * row[0]) for row in dc)
        headroom = min(min(row[2] - row[5], row[3] - row[4]) for row in dc)
        common_modes = [(row[2] + row[3]) / 2 for row in dc]
        gain_dc = ac[0][1]
        loss_62 = 20 * math.log10(interpolate(ac, 62.5e6) / gain_dc)
        loss_125 = 20 * math.log10(interpolate(ac, 125e6) / gain_dc)
        spectrum, totals = rows(case / 'noise.dat'), rows(case / 'noise_total.dat')
        if (len(spectrum) != 151 or len(totals) != 1 or
                abs(spectrum[0][0] - 1e3) > 1e-6 or abs(spectrum[-1][0] / 1e8 - 1) > 1e-8 or
                any(value < 0 for row in spectrum for value in row[1:])):
            raise ValueError('incomplete or negative noise observations')
        # ngspice reports amplitude density in V/sqrt(Hz). Independently
        # integrate its square over frequency and check the integrated plot.
        integrated = [math.sqrt(sum((b[0] - a[0]) * (a[i] ** 2 + b[i] ** 2) / 2
                                    for a, b in zip(spectrum, spectrum[1:]))) for i in (1, 2)]
        output_noise, input_noise = totals[0][1:3]
        checks = dict(gain=(1.2 if loaded else 2.4) <= gain <= (3.5 if loaded else 7),
                      linearity=linear_error < .02, headroom=headroom > .25,
                      common_mode=min(common_modes) > .8 and max(common_modes) < supply - .3,
                      supply_current=all(.0005 < row[7] < .006 for row in dc),
                      nyquist_response=loss_62 > -1.5, symbol_rate_response=loss_125 > -3,
                      input_noise=0 < input_noise < 100e-6,
                      noise_integration=all(abs(numeric - measured) < .02 * measured
                                            for numeric, measured in zip(integrated, (output_noise, input_noise))))
        result = dict(name=name, corner=corner, temperature_C=temperature, supply_V=supply,
                      differential_load_ohm=2000 if loaded else None, dc_gain=gain,
                      max_linearity_error_V=linear_error, min_drain_source_headroom_V=headroom,
                      output_common_mode_V=[min(common_modes), max(common_modes)],
                      loss_62p5MHz_dB=loss_62, loss_125MHz_dB=loss_125, checks=checks)
        result.update(noise_band_Hz=[1e3, 1e8], input_noise_rms_V=input_noise, output_noise_rms_V=output_noise,
                      independently_integrated_noise_rms_V=integrated)
        experiments.append(result)
        failures.extend(name + ': ' + check for check, passed in checks.items() if not passed)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != value for path, value in provenance.items()):
        raise ValueError('amplifier sources/model changed during characterization')
    report = dict(status='fail' if failures else 'pass', cases=len(experiments),
                  scope='DC/AC/noise IHP PSP characterization; no layout, mismatch or full-PHY qualification',
                  topology='basic/nmos_resistor_amplifier.hpp resistor/MOS topology, explicit charge plus intrinsic PSP charge',
                  transistor=dict(model='sg13_hv_nmos', width_m=50e-6, length_m=.45e-6, body='ground'),
                  same_equations_as_square_law=False, experiments=experiments, failures=failures,
                  source_sha256=provenance)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(experiments)} PSP amplifier PVT/load cases: {report["status"]}')
    if failures:
        raise ValueError('\n'.join(failures))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    characterize(parser.parse_args())
