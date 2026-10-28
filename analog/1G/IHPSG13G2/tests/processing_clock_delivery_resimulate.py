#!/usr/bin/env python3
"""Repeat a memory-limited clock simulation with its exact retained circuit."""
import argparse
import csv
import gzip
import json
import os
from pathlib import Path
import resource
import shutil
import sys
import tempfile

from processing_clock_delivery import ANALOG, HEADER, checked_rows, digest, qualify
from input_charge import verify_files
from tool_process import configure_cleanup, execute, memory_slot
from trace_storage import capture_trace


def run(args):
    original = json.loads(args.receipt.read_text())
    if original.get('status') != 'fail' or original.get('flow') not in ('veriloga', 'psp'):
        raise ValueError('requires a failed electrical clock-delivery receipt')
    source = Path(original['work'])
    if 'malloc: can\'t allocate' not in (source / 'ngspice.log').read_text():
        raise ValueError('requires a recorded simulator allocation failure')
    hashes = dict(original['source_sha256'])
    verify_files(hashes)
    ngspice = ANALOG / 'out/veriloga-toolchain/install/bin/ngspice'
    if str(ngspice.resolve()) not in hashes:
        raise ValueError('retained simulator is not identified')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    names = ['closed.cir', 'controller.so', 'design.va', 'live.json']
    if original['flow'] == 'veriloga':
        names.append('design.osdi')
    for name in names:
        shutil.copyfile(source / name, work / name)
        hashes[str(source / name)] = digest(source / name)
        hashes[str(work / name)] = digest(work / name)
    for path in [Path(__file__).resolve(), args.receipt.resolve(),
                 ANALOG / '1G/IHPSG13G2/input_charge.py']:
        hashes[str(path)] = digest(path)
    report = dict(status='running', flow=original['flow'], work=str(work),
        source_sha256=hashes, original_receipt=str(args.receipt.resolve()),
        scope=original['scope'], memory_limit_bytes=3 << 30,
        recovery='Entire transient repeated from time zero with identical deck, controller and models; '
                 'no partial waveform reuse or altered electrical acceptance')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        environment = dict(os.environ, OMP_NUM_THREADS='1')
        if original['flow'] == 'psp':
            pdk = ANALOG / 'pdk/IHPSG13G2'
            environment.update(PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                SPICE_USERINIT_DIR=str(pdk / 'ihp-sg13g2/libs.tech/ngspice'))
        # ngspice stores the saved vectors and copies observation expressions
        # before wrdata. Full 64 us traces need more than the old 1 GiB cap.
        with memory_slot(['clock-delivery-resimulation'], exclusive=True):
            with capture_trace(work / 'spice.dat') as capture, (work / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=' + str(3 << 30), '--', ngspice, '-b', 'closed.cir'],
                    cwd=work, env=environment, stdout=log, stderr=-2,
                    timeout=43200, check=True, memory_exclusive=False)
            report['trace_storage'] = capture.metadata
            save()
            if 'ngspice-47 done' not in (work / 'ngspice.log').read_text():
                raise ValueError('simulator did not complete the repeated transient')
            resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
            with gzip.open(work / 'spice.dat.gz', 'rt') as stream:
                names = next(stream).split()
                if len(names) != len(HEADER) or names[0] != 'time':
                    raise ValueError('changed repeated clock observations')
                rows = checked_rows((line.split() for line in stream if line.strip()),
                                    len(HEADER), 'repeated clock delivery')
            with (work / 'events.csv').open() as stream:
                records = list(csv.DictReader(stream))
            report['electrical'] = qualify(rows, records, original['flow'])
            report['trace_sha256'] = capture.metadata['original_sha256']
            verify_files(hashes)
        report['status'] = 'pass'
        print('PASS complete repeated clock delivery: ' + original['flow'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
