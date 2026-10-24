#!/usr/bin/env python3
"""Diagnose sampler charge cancellation in a retained continuous cold-start loop.

Only the physical dummy width changes. This does not qualify a new C++ class:
a selected geometry must be declared, exported and tested again in all flows.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent), str(SOURCE.parents[1]), str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests')]
from differential_receive_conversion import contract, read_spice
from hardware import configure_cleanup, execute
from trace_storage import capture_trace
from time_grid import check_observed_grid
import xspice_live


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    receipt = json.loads(args.receipt.read_text())
    if (receipt.get('flow') != 'psp' or receipt.get('slices') != 1 or receipt.get('gain') != 8 or
            {k for k, v in receipt.get('checks', {}).items() if not v} != {'sampler_acquisition'}):
        raise ValueError('requires the retained one-slice high-gain acquisition failure')
    original = Path(receipt['work'])
    metadata = json.loads((original / 'live.json').read_text())
    if not metadata.get('cold_start') or metadata.get('sampler_dummy_p_beta_A_per_V2') != .006:
        raise ValueError('unknown cold-start sampler baseline')
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    hashes = dict(receipt['source_sha256'])
    paths = [SOURCE, args.receipt.resolve(), args.ngspice.resolve(), SOURCE.with_name('differential_receive_conversion.py'),
        ANALOG / 'VerilogA/trace_storage.py', ANALOG / 'VerilogA/time_grid.py', SOURCE.parents[1] / 'xspice_live.py']
    paths += [p for p in original.iterdir() if p.is_file() and p.name != 'checks.json']
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('changed cold-start baseline source or trajectory')
    bindings = json.loads((original / 'bindings.json').read_text())['devices']
    dummy = [d['device'] for d in bindings if d['source'] == 'mos_track_hold.hpp' and d['model'] == 'sg13_hv_pmos' and d['width_um'] == 30]
    if len(dummy) != 1:
        raise ValueError('missing unique physical PMOS sampling dummy')
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={}, widths_um=args.widths,
        scope='continuous cold-start single-slice transistor sizing with the same live C++RTL controller and independent checks; physical dummy-width override only, no C++ model/full-bank/corner qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    save()
    try:
        (work / "source_snapshot.py").write_text(SOURCE.read_text())
        for width in args.widths:
            case = work / str(width)
            case.mkdir()
            for name in ('controller.so', 'columns.csv', 'live.json', 'conditions.json', 'timing-grid.json'):
                shutil.copyfile(original / name, case / name)
            deck = (original / 'cosim.cir').read_text()
            identifier = dummy[0]
            pattern = rf'^(Xphysical{identifier} (n\d+) (n\d+) \2 (n\d+) sg13_hv_pmos) w=30u l=0.45u ng=3$'
            deck, count = re.subn(pattern, rf'\1 w={width}u l=0.45u ng={(width + 9) // 10}', deck, flags=re.M)
            if count != 1:
                raise ValueError('changed dummy topology, original dimension or finger count')
            (case / 'cosim.cir').write_text(deck)
            with capture_trace(case / 'spice.dat') as captured, (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'cosim.cir'], cwd=case,
                    env=dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                        PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models)), stdout=log, stderr=-2, check=True, timeout=3600)
            text = (case / 'ngspice.log').read_text()
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', text, re.I):
                raise ValueError('cold sizing process simulation failed')
            raw = captured.path
            header = next(csv.reader((case / 'columns.csv').open()))
            rows = read_spice(raw, header)
            with (case / 'controls.csv').open() as source:
                controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(source)]
            result = contract(header, rows, controls, metadata)
            result['bridge'] = xspice_live.check_bridge(case, 1, lambda path, columns: read_spice(raw, columns))
            result['observed_grid'] = check_observed_grid(rows, json.loads((case / 'timing-grid.json').read_text()))
            result.update(waveform=str(raw), waveform_sha256=digest(raw),
                artifact_sha256={str(p): digest(p) for p in case.iterdir() if p.is_file()})
            report['cases'][str(width)] = result
            save()
            if width == 30:
                metrics = ('maximum_acquisition_error_V', 'maximum_hold_variation_V')
                if result['codes'] != receipt['codes'] or any(abs(result[k] - receipt[k]) > .0001 for k in metrics):
                    raise ValueError('baseline replay does not reproduce original cold-start failure')
            print('DIAGNOSTIC dummy', width, 'um acquisition V', result['maximum_acquisition_error_V'],
                'hold V', result['maximum_hold_variation_V'], 'checks', result['checks'], flush=True)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('sizing inputs changed')
        report['status'] = 'diagnostic_complete'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('receipt', 'ngspice', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--widths', type=int, nargs='+', choices=(20, 25, 30, 35, 40), default=[30, 25, 20])
    args = parser.parse_args()
    if args.widths[0] != 30 or len(set(args.widths)) != len(args.widths):
        parser.error('unique sizing widths must begin with the 30 um baseline')
    run(args)
