#!/usr/bin/env python3
"""Recheck a completed receiver transient using independently measured DC supply dependence.

The original simulation and report are preserved. All consumed source/tool/model
hashes must still match. Only additional DC operating points are simulated; the
original transient's accuracy, hold, phase and delivery limits remain unchanged.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from receive_dc_surface import Surface, surface_deck
from receive_conversion import check
from hardware import configure_cleanup, execute
from xspice_live import check_bridge


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()


def run(args):
    original, work = args.original.resolve(), args.work.resolve()
    if original == work:
        raise ValueError('supply recheck requires a separate work directory')
    work.mkdir(parents=True, exist_ok=True)
    old_report = original / 'checks.json'
    old = json.loads(old_report.read_text())
    if (old.get('status') not in ('pass', 'fail') or old.get('backend') != 'continuous XSPICE C++RTL/PSP'
            or (old['status'] == 'fail' and old.get('error') != 'PSP receive input/supply outside characterized DC transfer')
            or not old.get('source_sha256')):
        raise ValueError('a completed, source-tracked continuous receiver experiment is required')
    tracked = old['source_sha256']
    changed = [name for name, expected in tracked.items() if digest(name) != expected]
    if changed:
        raise ValueError('original simulation inputs changed: ' + ', '.join(changed))
    model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    if str(args.ngspice.resolve()) not in tracked or str(model_root / '.spiceinit') not in tracked:
        raise ValueError('recheck tool/PDK differs from the original experiment')
    files = [old_report] + [original / name for name in ('design.va', 'bench.cir', 'ihp.cir',
        'cosim.cir', 'dc.cir', 'dc.dat', 'controller.so', 'spice.dat', 'ngspice.log',
        'controls.csv', 'bridge_events.csv', 'columns.csv', 'live.json')]
    inputs = {str(path): digest(path) for path in files}
    sources = {str(path): digest(path) for path in (SOURCE, SOURCE.parents[1] / 'receive_dc_surface.py')}
    report = dict(status='running', original_report=str(old_report), original_status=old['status'],
        original_input_sha256=tracked, retained_artifact_sha256=inputs, source_sha256=sources,
        slices=old['slices'], bursts=old['bursts'], new_transient_run=False,
        scope='same completed PSP trajectory checked against measured differential-input/achieved-supply DC surface; '
              'original absolute accuracy, supply, hold, normalization and phase limits retained')
    destination = work / 'checks.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    try:
        for name in ('spice.dat', 'controls.csv', 'bridge_events.csv', 'columns.csv', 'live.json'):
            path = work / name
            target = original / name
            if path.exists() or path.is_symlink():
                if not path.is_symlink() or path.resolve() != target.resolve():
                    raise ValueError('recheck work directory contains a conflicting artifact: ' + name)
            else:
                path.symlink_to(target)
        (work / 'dc_surface.cir').write_text(surface_deck((original / 'dc.cir').read_text()))
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(model_root), OMP_THREAD_LIMIT='1')
        with (work / 'dc_surface.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'dc_surface.cir'], cwd=work,
                    env=environment, stdout=log, stderr=-2, check=True, timeout=300)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (work / 'dc_surface.log').read_text(), re.I):
            raise ValueError('receiver supply characterization failed')
        with (work / 'supply_surface.dat').open() as stream:
            header = next(stream).split()
            expected = ['v-sweep', 'characterization_supply'] + ['observation' + str(i) for i in range(6)]
            if header != expected:
                raise ValueError('changed supply-surface output columns')
            rows = [[float(value) for value in line.split()] for line in stream if line.strip()]
        transfer = Surface(rows)
        with (original / 'dc.dat').open() as stream:
            next(stream)
            old_rows = [[float(value) for value in line.split()] for line in stream if line.strip()]
        nominal = sorted((row for row in rows if abs(row[1] - 3.3) < 1e-8), key=lambda row: row[0])
        if len(old_rows) != 5 or any(abs(a - b) > 1e-6
                for before, after in zip(old_rows, nominal) for a, b in zip(before, [after[0], *after[2:]])):
            raise ValueError('expanded DC characterization disagrees with retained nominal operating points')
        report['nominal_dc_consistent'] = True
        report['dc_operating_points'] = len(rows)
        report.update(check_bridge(work, old['slices']))
        report.update(check(work, 'veriloga', old['slices'], old['bursts'], transfer))
        for name, expected in {**tracked, **inputs, **sources}.items():
            if digest(name) != expected:
                raise ValueError('retained simulation evidence changed during recheck: ' + name)
        report['new_artifact_sha256'] = {str(path): digest(path) for path in
            (work / 'dc_surface.cir', work / 'dc_surface.log', work / 'supply_surface.dat')}
        report['status'] = 'pass'
        print('PASS retained receiver trajectory with supply characterization:', report['codes'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        destination.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'work', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
