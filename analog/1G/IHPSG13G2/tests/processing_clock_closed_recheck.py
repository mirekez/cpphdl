#!/usr/bin/env python3
"""Recheck a completed closed-clock simulation with bounded full-resolution storage."""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path
import resource
import shutil
import tempfile

import processing_clock_closed as delivery
from compact_trace import checked_rows

SOURCE = Path(__file__).resolve()


def run(args):
    original = json.loads(args.proof.read_text())
    flow = original['flow']
    if flow not in ('cpp', 'veriloga', 'psp') or original.get('status') not in ('pass', 'running', 'fail'):
        raise ValueError('unsupported retained run')
    old_hashes = original.get('source_sha256', {})
    validator = delivery.SOURCE.resolve()
    for name, expected in old_hashes.items():
        if delivery.digest(Path(name)) != expected:
            raise ValueError('simulation source or tool changed: ' + name)
    origin = Path(original['work'])
    trace_name = 'trace.csv' if flow == 'cpp' else 'spice.dat'
    trace = origin / trace_name
    if not trace.exists():
        trace = origin / (trace_name + '.gz')
    controls = origin / ('controls.csv' if flow == 'cpp' else 'events.csv')
    consumed = [args.proof.resolve(), trace, controls]
    if flow != 'cpp':
        consumed += [origin / name for name in ('closed.cir', 'controller.so', 'design.va', 'ngspice.log')]
    input_hashes = {str(path): delivery.digest(path) for path in consumed}
    hashes = dict(old_hashes)
    for path in (validator, SOURCE, delivery.ANALOG / 'VerilogA/compact_trace.py'):
        hashes[str(path)] = delivery.digest(path)
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='recheck-', dir=args.work.resolve()))
    report = dict(status='running', flow=flow, work=str(work), source_sha256=hashes,
        recheck=dict(original_status=original['status'], original_work=str(origin),
                     original_simulation_source_sha256=old_hashes,
                     input_sha256=input_hashes,
                     scope='unchanged simulated circuit/tools; new validator reads every retained sample'),
        scope=original['scope'])

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        # Bound the checker independently of the shared scheduling lock. A
        # future memory regression must fail this process, not stall routing.
        resource.setrlimit(resource.RLIMIT_AS, (1536 << 20, 1536 << 20))
        opener = gzip.open if trace.suffix == '.gz' else open
        with opener(trace, 'rt') as stream:
            if flow == 'cpp':
                reader = csv.reader(stream)
                if next(reader) != delivery.HEADER:
                    raise ValueError('changed native observations')
            else:
                names = next(stream).split()
                if len(names) != len(delivery.HEADER) or names[0] != 'time':
                    raise ValueError('changed SPICE observations')
                reader = (line.split() for line in stream if line.strip())
            rows = checked_rows(reader, len(delivery.HEADER), str(trace))
        with controls.open() as stream:
            records = list(csv.DictReader(stream))
        report['electrical'] = delivery.qualify(rows, records, flow)
        report['samples'] = len(rows)
        del rows
        checksum = hashlib.sha256()
        with opener(trace, 'rb') as stream, gzip.open(work / (trace_name + '.gz'), 'wb', compresslevel=1) as target:
            for block in iter(lambda: stream.read(1 << 20), b''):
                checksum.update(block)
                target.write(block)
        report['trace_sha256'] = checksum.hexdigest()
        if original.get('trace_sha256') not in (None, report['trace_sha256']):
            raise ValueError('retained waveform differs from original receipt')
        shutil.copyfile(controls, work / controls.name)
        for source in (hashes, input_hashes):
            if any(delivery.digest(Path(name)) != expected for name, expected in source.items()):
                raise ValueError('recheck inputs changed')
        report['status'] = 'pass'
        print(f'PASS full-resolution {flow} recheck: {report["samples"]} samples')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    # Six packed columns and a hard 1.5 GiB cap permit checking beside the
    # large router. Original simulation receipts are never overwritten.
    run(args)
