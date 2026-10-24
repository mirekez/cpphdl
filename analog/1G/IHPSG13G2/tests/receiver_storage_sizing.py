#!/usr/bin/env python3
"""Test modest hold-storage margin with an independently reproduced baseline."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parent))
from receiver_charge_sizing import initialized_deck, metrics, read_spice, HEADER, WARMUP, digest
from trace_storage import capture_trace
from hardware import configure_cleanup, execute


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='storage-', dir=root))
    receipt = json.loads(args.receipt.read_text())
    if receipt.get('sampler_dummy_p_width_um') != 30 or not receipt.get('charge_defined_nodes'):
        raise ValueError('requires the actual charge-defined 30 um receiver reference')
    reference = receipt['cases'][str(args.code)]
    for path, sha in receipt['source_sha256'].items():
        if digest(path) != sha:
            raise ValueError('stale continuous-startup reference')
    for path, sha in reference['artifact_sha256'].items():
        if digest(path) != sha:
            raise ValueError('changed reference artifact')
    case = Path(receipt['run_directory']) / str(args.code)
    rows = read_spice(Path(reference['waveform']), HEADER)
    original = (case / 'physical.cir').read_text()
    observations = dict((int(i), n) for i, n in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    held = {observations[7 + 2 * i]: rows[0][8 + 2 * i] for i in range(16)}
    base = initialized_deck(original, held)
    expected = metrics(rows, WARMUP)
    hashes = receipt['source_sha256'] | {str(SOURCE): digest(SOURCE), str(args.receipt.resolve()): digest(args.receipt),
        str(SOURCE.with_name('receiver_charge_sizing.py')): digest(SOURCE.with_name('receiver_charge_sizing.py'))}
    report = dict(status='running', source_sha256=hashes, code=args.code, work=str(work), cases={},
                  continuous_startup_reference=expected,
                  scope='initialized full 16-slice storage diagnostic; baseline agreement required; no complete-startup/PVT/closed-ADC qualification')

    def validate_baseline(result):
        difference = max(abs(a - b) for a, b in zip(result['per_sample_error_V'], expected['per_sample_error_V']))
        if difference > .001:
            raise ValueError('initialized baseline does not reproduce continuous startup within 1 mV')
        report['baseline_reproduction_maximum_difference_V'] = difference

    try:
        storages = [1.5, 1.75]
        if args.baseline:
            baseline = json.loads(args.baseline.read_text())
            if baseline.get('status') != 'diagnostic_complete':
                raise ValueError('retained baseline diagnostic is incomplete')
            for path, sha in baseline['source_sha256'].items():
                if digest(path) != sha:
                    raise ValueError('stale retained baseline')
            retained = baseline['cases']['30']
            for path, sha in retained['artifact_sha256'].items():
                if digest(path) != sha:
                    raise ValueError('changed retained baseline artifact')
            if base != (Path(retained['waveform']).parent / 'physical.cir').read_text():
                raise ValueError('retained initialized circuit differs from the regenerated baseline')
            result = metrics(read_spice(Path(retained['waveform']), HEADER), 0)
            if any(result[key] != retained[key] for key in result):
                raise ValueError('retained baseline checking result changed')
            validate_baseline(result)
            report['cases']['1.5'] = result | dict(reused_baseline=str(args.baseline.resolve()))
            hashes[str(args.baseline.resolve())] = digest(args.baseline)
            storages = [1.75]
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        for storage in storages:
            current = work / str(storage)
            current.mkdir()
            deck = base
            if storage != 1.5:
                for node in held:
                    candidates = [line for line in deck.splitlines() if line.startswith('Cdevice')
                                  and line.split()[1:3] == [node, '0']
                                  and abs(float(line.split()[3]) - 1.5e-12) < 1e-24]
                    if len(candidates) != 1:
                        raise ValueError('changed hold-storage topology')
                    fields = candidates[0].split()
                    fields[3] = format(storage * 1e-12, '.17g')
                    deck = deck.replace(candidates[0], ' '.join(fields), 1)
            (current / 'physical.cir').write_text(deck)
            with capture_trace(current / 'spice.dat') as captured, (current / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=current, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=2400)
            log = (current / 'ngspice.log').read_text()
            if 'ngspice-47 done' not in log or re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', log, re.I):
                raise ValueError('storage diagnostic failed')
            samples = read_spice(captured.path, HEADER)
            if abs(samples[-1][0] - 80e-9) > 1e-14:
                raise ValueError('incomplete storage diagnostic')
            result = metrics(samples, 0)
            result.update(waveform=str(captured.path), waveform_capture=captured.metadata,
                          artifact_sha256={str(p): digest(p) for p in current.iterdir() if p.is_file()})
            if storage == 1.5:
                validate_baseline(result)
            report['cases'][str(storage)] = result
            print(args.code, storage, 'pF, maximum diagnostic error V', result['maximum_acquisition_error_V'], flush=True)
            (root / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        if any(digest(p) != sha for p, sha in hashes.items()):
            raise ValueError('storage diagnostic inputs changed')
        report['status'] = 'diagnostic_complete'
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
    for name in ('work', 'receipt', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--code', choices=(4, 8), type=int, required=True)
    run(parser.parse_args())
