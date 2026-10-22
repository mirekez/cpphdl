#!/usr/bin/env python3
"""Qualify the actual analog pad's normal DC and small-signal loading."""
import argparse
import cmath
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from tool_process import configure_cleanup, execute


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def rows(path, width, count):
    lines = path.read_text().splitlines()
    if not lines or len(lines[0].split()) != width:
        raise ValueError('changed pad observation interface')
    data = [[float(x) for x in line.split()] for line in lines[1:] if line.strip()]
    if len(data) != count or any(len(r) != width or not all(math.isfinite(v) for v in r) for r in data):
        raise ValueError('incomplete/nonfinite pad observations')
    return data


def check(case, load):
    dc = rows(case / 'dc.dat', 6, 9)
    if any(abs(row[0] - (2.4 + i * .1)) > 1e-9 for i, row in enumerate(dc)):
        raise ValueError('pad operating-range sweep changed')
    leakage = max(abs(r[3]) for r in dc)
    dc_error = max(abs(r[0] - r[col]) for r in dc for col in (1, 2))
    if leakage >= 5e-6 or dc_error >= .002:
        raise ValueError(f'pad normal DC leakage/delivery failed: {leakage}, {dc_error}')
    measurements = []
    for name, frequency in (('ac_low', 1e6), ('ac_high', 125e6)):
        row = rows(case / (name + '.dat'), 7, 1)[0]
        if abs(row[0] / frequency - 1) > 1e-12:
            raise ValueError('pad AC frequency differs')
        pad, core, current = [complex(*row[i:i + 2]) for i in (1, 3, 5)]
        admittance = -current / pad
        capacitance = admittance.imag / (2 * math.pi * frequency)
        transfer = abs(core / pad)
        # Includes the 586.899 ohm secondary resistor, real protection-device
        # charge and external load; these are interface budgets, not extracted RC.
        if not (load < capacitance * 1e12 < load + 2 and admittance.real > 0
                and .75 < transfer <= 1.001 and .85 < abs(pad) <= 1.001):
            raise ValueError(f'pad AC load/transfer failed: {capacitance}, {transfer}, {abs(pad)}')
        measurements.append(dict(frequency_Hz=frequency, capacitance_pF=capacitance * 1e12,
            secondary_gain=transfer, primary_gain=abs(pad), secondary_phase_rad=cmath.phase(core / pad)))
    return dict(maximum_leakage_A=leakage, maximum_dc_error_V=dc_error, ac=measurements)


def run(args):
    pdk, work = args.pdk.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    tech = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    models = tech / 'models'
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_io/spice/sg13g2_io.spice'
    paths = [SOURCE, SOURCE.parents[1] / 'tool_process.py', args.ngspice.resolve(), tech / '.spiceinit', library]
    paths += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', cases={}, source_sha256=hashes,
        scope='actual IHP analog pad normal operation at 3.3/1.2 V, 2.4..3.2 V input; DC/AC loading, no ESD stress rating or connected PHY proof')
    environment = dict(os.environ, PDK_ROOT=str(pdk), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(tech))
    try:
        for corner, temperature, load in itertools.product(('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'),
                                                          (-40, 27, 125), (.15, 1.0)):
            name = f'{corner}_{temperature}_{load}'
            case = work / name
            case.mkdir(exist_ok=True)
            deck = f'''Actual IHP analog pad normal operating load
.lib "{models / 'cornerMOSlv.lib'}" {corner}
.lib "{models / 'cornerMOShv.lib'}" {corner}
.lib "{models / 'cornerRES.lib'}" res_typ
.lib "{models / 'cornerDIO.lib'}" dio_tt
.include "{library}"
.temp {temperature}
.global sub!
Vdd core_supply 0 1.2
Vio io_supply 0 3.3
Vcommand source 0 DC 2.8 AC 1
Rdrive source pad 50
Xpad pad padres core_supply 0 io_supply 0 sg13g2_IOPadAnalog
Cload padres 0 {load}p
.control
set wr_singlescale
set wr_vecnames
set numdgt=17
dc Vcommand 2.4 3.2 .1
wrdata dc.dat v(pad) v(padres) i(Vcommand) i(Vio) i(Vdd)
ac lin 1 1Meg 1Meg
wrdata ac_low.dat v(pad) v(padres) i(Vcommand)
ac lin 1 125Meg 125Meg
wrdata ac_high.dat v(pad) v(padres) i(Vcommand)
quit
.endc
.end
'''
            (case / 'pad.cir').write_text(deck)
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'pad.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=60)
            if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found', (case / 'ngspice.log').read_text(), re.I):
                raise ValueError('IHP pad simulation failed: ' + name)
            result = check(case, load)
            result['artifact_sha256'] = {str(p): digest(p) for p in case.iterdir() if p.is_file()}
            report['cases'][name] = result
            report_path.write_text(json.dumps(report, indent=2) + '\n')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('IHP pad qualification inputs changed')
        report['status'] = 'pass'
        print('PASS 30 actual-pad operating-load cases')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
