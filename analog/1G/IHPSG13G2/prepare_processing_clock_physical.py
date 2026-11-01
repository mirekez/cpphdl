#!/usr/bin/env python3
"""Snapshot the independently replayed clock controller for the physical flow."""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil

SOURCE = Path(__file__).resolve()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def unchanged(hashes):
    if not hashes or any(not Path(p).is_file() or digest(p) != h for p, h in hashes.items()):
        raise ValueError('missing or changed controller evidence')


def raw_digest(record):
    path = Path(record['path'])
    if digest(path) != record['archive_sha256']:
        raise ValueError('changed compressed controller trajectory')
    h = hashlib.sha256()
    with gzip.open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    if h.hexdigest() != record['raw_sha256']:
        raise ValueError('controller trajectory hash mismatch')
    return h.hexdigest()


def prepare(native_proof, cell_proof, output, extra_sources=()):
    native_proof, cell_proof, output = (Path(p).resolve() for p in (native_proof, cell_proof, output))
    native, cells = (json.loads(p.read_text()) for p in (native_proof, cell_proof))
    if (native.get('status') != 'pass' or cells.get('status') != 'pass' or
            native.get('events') != 698640 or native.get('drift_recoveries') != 2 or native.get('qualified_curves') != 5 or
            native.get('rejected_curves') != 3 or native.get('load_step_cases') != 6 or cells.get('acquisition_period_ns') != 1.25 or
            cells.get('reference_period_ns') != 8 or
            not isinstance(cells.get('setup_slack_ns'), (int, float)) or
            not math.isfinite(cells['setup_slack_ns']) or cells['setup_slack_ns'] < 0):
        raise ValueError('complete passing 800 MHz native and cell proofs are required')
    for proof in (native, cells):
        unchanged(proof.get('source_sha256', {}))
    unchanged(native.get('rtl_sha256', {}))
    unchanged(cells.get('artifact_sha256', {}))
    if cells['source_sha256'].get(str(native_proof)) != digest(native_proof):
        raise ValueError('cell proof used a different native proof')
    traces = [native['traces'][n] for n in ('native', 'rtl')]
    traces += [cells['traces'][n] for n in ('mapped', 'resized')]
    if len({raw_digest(record) for record in traces}) != 1:
        raise ValueError('controller flows disagree')
    mapped = Path(cells['work'])
    names = ('mapped.v', 'mapped.json', 'cells.v')
    for name in names:
        if cells['artifact_sha256'].get(str(mapped / name)) != digest(mapped / name):
            raise ValueError('untracked physical input: ' + name)
    module = json.loads((mapped / 'mapped.json').read_text())['modules']['ProcessingClockControl']
    if not module['ports'] or not module['cells']:
        raise ValueError('empty controller mapping')
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError('physical input snapshot must be fresh')
    hashes = dict(native['source_sha256'], **cells['source_sha256'])
    hashes.update(native['rtl_sha256'])
    hashes.update(cells['artifact_sha256'])
    for path in (SOURCE, native_proof, cell_proof, *extra_sources, *[Path(r['path']) for r in traces]):
        hashes[str(path)] = digest(path)
    for name in names:
        shutil.copyfile(mapped / name, output / name)
        if digest(output / name) != cells['artifact_sha256'][str(mapped / name)]:
            raise ValueError('controller netlist changed while copying')
    with gzip.open(traces[0]['path'], 'rb') as src, (output / 'cpp_trace.csv').open('wb') as dst:
        shutil.copyfileobj(src, dst)
    if digest(output / 'cpp_trace.csv') != traces[0]['raw_sha256']:
        raise ValueError('copied native trajectory changed')
    unchanged(hashes)
    report = dict(status='pass', module='ProcessingClockControl', native_mapped_exact=True,
                  mapped_sha256=digest(output / 'mapped.v'), source_sha256=hashes,
                  scope='verified functional/timing input snapshot; physical qualification is still required')
    (output / 'hardware_checks.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-proof', 'cell-proof', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    prepare(args.native_proof, args.cell_proof, args.output)
