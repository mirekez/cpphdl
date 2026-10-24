#!/usr/bin/env python3
"""Characterize the pinned two-terminal MOSCAP before using it for bias storage.

This is a process-device experiment, not a transmitter qualification. Retain
voltage-dependent capacitance, loss and leakage instead of substituting an
ideal capacitor and assuming the MOS implementation has the same equations.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[2] / 'tests'))
from hardware import configure_cleanup, execute


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='cv-', dir=root))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.ngspice.resolve(), models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    if args.profile == 'lv_bias':
        geometry = dict(model='sg13_moscap_n', width_um=30, length_um=30, copies=40,
                        total_gate_area_um2=36000)
        library = f'.lib "{models / "models/cornerMOSCAP.lib"}" moscap_tt'
        instance = 'Xstorage gate 0 sg13_moscap_n w=30u l=30u m=40'
        biases = (0., .25, .50, .75, .85, .95, 1.05, 1.10, 1.30)
        operating_low, operating_high, floor = .85, 1.10, 400e-12
        if args.corner != 'mos_tt':
            raise ValueError('dedicated MOSCAP has a TT deck, not MOS transistor corners')
    else:
        geometry = dict(model='sg13_hv_nmos', width_um=100, length_um=.5, fingers=10, copies=220,
                        total_gate_area_um2=11000, drain_source_body='ground')
        library = f'.lib "{models / "models/cornerMOShv.lib"}" {args.corner}'
        instance = 'Xstorage 0 gate 0 0 sg13_hv_nmos w=100u l=.5u ng=10 m=220'
        biases = (0., .50, 1., 1.5, 1.6, 1.7, 1.8, 1.9, 2., 2.2, 2.4)
        operating_low, operating_high, floor = 1.6, 2.4, 50e-12
    report = dict(status='running', source_sha256=hashes, work=str(work), cases={},
                  profile=args.profile, transistor_corner=args.corner, geometry=geometry,
                  required_storage_floor_F=floor,
                  scope='pinned process-device AC and DC characterization; no transmitter, mismatch, layout or signoff')
    try:
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        for temperature in (-40, 27, 125):
            case = work / str(temperature)
            case.mkdir()
            lines = ['IHP MOSCAP bias-storage characterization',
                     library, f'.temp {temperature}', 'Vbias gate 0 dc .95 ac 1', instance,
                     '.control', 'set num_threads=1', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
                     f'dc Vbias 0 {biases[-1]} .01', 'wrdata leakage.dat i(Vbias)']
            for index, bias in enumerate(biases):
                lines += [f'alter Vbias dc = {bias:.17g}', 'ac dec 10 100000 1000000000',
                          'let storage = -imag(i(Vbias))/(2*pi*frequency)',
                          'let conductance = -real(i(Vbias))',
                          f'wrdata ac{index}.dat storage conductance']
            lines += ['quit', '.endc', '.end']
            (case / 'physical.cir').write_text('\n'.join(lines) + '\n')
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=300)
            log = (case / 'ngspice.log').read_text()
            if ('ngspice-47 done' not in log or re.search(r'\b(error|fatal|unknown|aborted)\b|timestep too small', log, re.I)):
                raise ValueError('failed MOSCAP device simulation')
            leakage = [[float(v) for v in line.split()] for line in (case / 'leakage.dat').read_text().splitlines()[1:]]
            if (len(leakage) != round(biases[-1] * 100) + 1 or any(len(row) != 2 or not all(math.isfinite(v) for v in row) for row in leakage)
                    or any(abs(row[0] - index * .01) > 1e-12 for index, row in enumerate(leakage))):
                raise ValueError('incomplete MOSCAP leakage sweep')
            result = dict(maximum_gate_leakage_A=max(abs(row[1]) for row in leakage), biases={})
            for index, bias in enumerate(biases):
                raw = [[float(v) for v in line.split()] for line in (case / f'ac{index}.dat').read_text().splitlines()[1:]]
                # Ngspice retains a zero imaginary column for division by its
                # complex frequency vector. Verify it before taking real C.
                if (len(raw) != 41 or any(len(row) != 4 or not all(math.isfinite(v) for v in row)
                                         or abs(row[2]) > 1e-24 for row in raw)):
                    raise ValueError('incomplete or complex MOSCAP capacitance')
                rows = [[row[0], row[1], row[3]] for row in raw]
                if (any(len(row) != 3 or not all(math.isfinite(v) for v in row) for row in rows)
                        or abs(rows[0][0] - 1e5) > 1e-5 or abs(rows[-1][0] - 1e9) > 1):
                    raise ValueError('incomplete MOSCAP frequency sweep')
                if any(row[1] <= 0 or row[2] < -1e-12 for row in rows):
                    raise ValueError('nonpassive MOSCAP AC result')
                result['biases'][str(bias)] = dict(minimum_capacitance_F=min(row[1] for row in rows),
                    maximum_capacitance_F=max(row[1] for row in rows),
                    capacitance_100MHz_F=rows[30][1],
                    maximum_loss_tangent=max(row[2] / (2 * math.pi * row[0] * row[1]) for row in rows))
            operating = [result['biases'][str(bias)] for bias in biases if operating_low <= bias <= operating_high]
            result['minimum_operating_capacitance_F'] = min(v['minimum_capacitance_F'] for v in operating)
            result['meets_storage_floor'] = result['minimum_operating_capacitance_F'] >= floor
            result['gate_leakage_below_50uA'] = result['maximum_gate_leakage_A'] < 50e-6
            result['artifact_sha256'] = {str(p): digest(p) for p in case.iterdir() if p.is_file()}
            report['cases'][str(temperature)] = result
            print(temperature, 'minimum operating pF', result['minimum_operating_capacitance_F'] * 1e12,
                  'maximum leakage uA', result['maximum_gate_leakage_A'] * 1e6, flush=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('MOSCAP characterization inputs changed')
        report['suitable_for_transmitter_trial'] = all(
            d['meets_storage_floor'] and d['gate_leakage_below_50uA'] for d in report['cases'].values())
        if not report['suitable_for_transmitter_trial']:
            raise ValueError('device did not meet its declared storage or leakage floor')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--profile', choices=('lv_bias', 'hv_cascode'), default='lv_bias')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    run(parser.parse_args())
