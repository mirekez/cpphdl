#!/usr/bin/env python3
"""Recover complete clock traces after an interrupted checker or archive step."""
import argparse
import csv
import gc
import json
from pathlib import Path
import resource
import sys
import tempfile

from processing_clock_delivery import ANALOG, HEADER, digest, qualify, read_spice
from tool_process import memory_slot
from trace_storage import archive_trace


def run(args):
    original = json.loads(args.receipt.read_text())
    if original.get('flow') not in ('psp', 'veriloga'):
        raise ValueError('requires a process or Verilog-A delivery simulation receipt')
    source = Path(original['work'])
    if 'ngspice-47 done' not in (source / 'ngspice.log').read_text():
        raise ValueError('simulator completion record is missing')
    trace = source / 'spice.dat'
    hashes = dict(original['source_sha256'])
    for name in ['closed.cir', 'controller.so', 'events.csv', 'ngspice.log', 'design.va', 'live.json']:
        hashes[str(source / name)] = digest(source / name)
    for p in [Path(__file__).resolve(), ANALOG / 'VerilogA/trace_storage.py', args.receipt.resolve()]:
        hashes[str(p)] = digest(p)
    if any(digest(Path(p)) != h for p, h in hashes.items()):
        raise ValueError('original simulation source changed')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='recheck-', dir=args.work.resolve()))
    report = dict(status='running', flow=original['flow'], work=str(work),
                  original_receipt=str(args.receipt.resolve()), source_sha256=hashes,
                  scope=original['scope'],
                  recovery='Recheck every sample with unchanged acceptance; preserve the original failed/interrupted receipt. Generated simulation artifacts are hashed at recovery.')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        with memory_slot(['processing-clock-delivery-recheck'], exclusive=True):
            resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
            rows = read_spice(trace, HEADER)
            with (source / 'events.csv').open() as stream:
                events = list(csv.DictReader(stream))
            report['electrical'] = qualify(rows, events, original['flow'])
            report['samples'] = len(rows)
            report['trace_sha256'] = digest(trace)
            del rows
            gc.collect()
            if any(digest(Path(p)) != h for p, h in hashes.items()):
                raise ValueError('qualification inputs changed during recheck')
            archive, metadata = archive_trace(trace)
            report.update(status='pass', archive=str(archive), trace_storage=metadata)
        print('PASS recovered complete clock delivery: ' + original['flow'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
