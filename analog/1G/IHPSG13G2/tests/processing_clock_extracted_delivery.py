#!/usr/bin/env python3
"""Exercise both extracted clock macros with the real C++ calibration controller."""
import argparse
import csv
import gzip
import json
import math
import os
from pathlib import Path
import resource
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent), str(ANALOG / 'VerilogA'),
               str(ANALOG / '1G/tests'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from extracted_clock_delivery import deck, GATE_PORTS
from processing_clock_oscillator_layout import PORTS
from processing_clock_delivery import qualify, HEADER
from compact_trace import checked_rows
from compare import edges
from extract_analog import digest
from hardware import source_dependencies
from input_charge import verify_files
from tool_process import execute, configure_cleanup, memory_slot
from trace_storage import capture_trace
from xspice_live import headers


def run(args):
    hashes = {}
    for directory, proof_path, ports, count, gds_name in (
            (args.oscillator, args.oscillator_layout, PORTS, 294, 'oscillator.gds'),
            (args.gate, args.gate_layout, GATE_PORTS, 104, 'gate.gds')):
        extraction_path = directory.resolve() / 'extraction.json'
        extraction = json.loads(extraction_path.read_text())
        layout = json.loads(proof_path.read_text())
        if (extraction.get('status') != 'pass' or extraction.get('ports') != ports
                or layout.get('status') != 'pass' or layout.get('lvs', {}).get('instantiated_devices') != count):
            raise ValueError('requires checked physical macro geometry and conserved extraction')
        for record in (extraction, layout):
            for key in ('source_sha256', 'artifact_sha256'):
                verify_files(record[key])
                hashes.update(record[key])
        gds = {p: h for p, h in layout['artifact_sha256'].items() if Path(p).name == gds_name}
        if len(gds) != 1 or any(extraction['source_sha256'].get(p) != h for p, h in gds.items()):
            raise ValueError('layout and extraction describe different physical geometry')
        hashes.update({str(p): digest(p) for p in (extraction_path, proof_path.resolve())})
    host = SOURCE.with_name('sim_processing_clock_delivery.cpp')
    hashes.update(source_dependencies([host], ANALOG.parent, args.cxx,
        ['-std=c++17', '-I' + str(ANALOG.parent / 'include'), '-I' + str(headers(args.ngspice))]))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, args.ngspice.resolve(),
        SOURCE.with_name('processing_clock_delivery.py'), SOURCE.with_name('processing_clock_closed.py'),
        ANALOG / 'VerilogA/compact_trace.py', ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_storage.py',
        ANALOG / '1G/tests/hardware.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    files += [SOURCE.parents[1] / name for name in ('extracted_clock_delivery.py',
        'oscillator_extracted_process.py', 'resistor_body_extraction.py', 'resistor_body_model.py',
        'extracted_connections.py', 'reduce_extracted_wires.py', 'input_charge.py', 'xspice_live.py')]
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        startup_only=args.startup_only, scope='connected extracted oscillator/gate with live controller at TT; '
        'excludes field/contact calibration, physical controller drivers, inter-macro routes, '
        'clock tree, PVT, mismatch, noise and full-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(name, argv, timeout):
        with (work / (name + '.log')).open('w') as log:
            execute(argv, cwd=work, env=dict(os.environ, OMP_NUM_THREADS='1',
                PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent)),
                stdout=log, stderr=-2, check=True, timeout=timeout, memory_exclusive=False)

    save()
    try:
        model_directory = SOURCE.parents[1] / 'generated'
        stimulus, report['binding'] = deck(args.oscillator.resolve(), args.gate.resolve(),
            model_directory / 'OneGIHPProcessingClockOscillator.va',
            model_directory / 'OneGIHPProcessingClockGate.va', models, 512e-9 if args.startup_only else 64e-6)
        (work / 'closed.cir').write_text(stimulus)
        command('host-build', [args.cxx, '-std=c++17', '-O1', '-fPIC', '-shared',
            '-I' + str(ANALOG.parent / 'include'), '-I' + str(headers(args.ngspice)),
            str(host), '-o', str(work / 'controller.so')], 120)
        save()
        # Preserve every output byte without needing space for both a raw
        # multi-gigabyte trace and its archive at the end of the simulation.
        # The complete run retains millions of vectors before wrdata; the
        # startup-only run fits the smaller limit.
        simulation_memory = (1 if args.startup_only else 3) << 30
        report['simulation_memory_limit_bytes'] = simulation_memory
        save()
        with capture_trace(work / 'spice.dat') as capture:
            command('ngspice', ['prlimit', '--as=' + str(simulation_memory), '--', str(args.ngspice.resolve()), '-b', 'closed.cir'],
                    1800 if args.startup_only else 43200)
        report['trace_storage'] = capture.metadata
        save()
        # Large full-resolution checking serializes with routing and other
        # checkers, while the simulator itself has the explicit bound above.
        with memory_slot(['extracted-clock-checker'], exclusive=True):
            resource.setrlimit(resource.RLIMIT_AS, (3 << 30, 3 << 30))
            qualify_trace(work, args, report)
        verify_files(hashes)
        for name in ('closed.cir', 'controller.so', 'events.csv', 'spice.dat.gz'):
            report.setdefault('artifact_sha256', {})[str(work / name)] = digest(work / name)
        print(report['status'] + ': connected extracted oscillator, gate and live controller')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def qualify_trace(work, args, report):
    with gzip.open(work / 'spice.dat.gz', 'rt') as stream:
        names = next(stream).split()
        if len(names) != len(HEADER) or names[0] != 'time':
            raise ValueError('changed extracted clock observations')
        rows = checked_rows((line.split() for line in stream if line.strip()), len(HEADER), 'extracted clock')
    with (work / 'events.csv').open() as stream:
        records = list(csv.DictReader(stream))
    if args.startup_only:
        if (not rows or rows[0][0] > 1e-12 or rows[-1][0] < 512e-9 - 1e-15
                or any(not all(math.isfinite(v) for v in row) for row in rows)):
            raise ValueError('incomplete extracted startup trajectory')
        raw = edges(rows, 1, .6, 8e-9, 512e-9)
        steady_supply = [row[4] for row in rows if row[0] >= 128e-9]
        if (len(raw) < 5 or not records or not steady_supply or min(steady_supply) < 1.1
                or any(int(row['ready_ref']) or int(row['ready_pcs']) or int(row['fault']) for row in records)
                or any(abs(row[6]) > .12 or abs(row[7]) > .12 for row in rows if row[0] >= 8e-9)):
            raise ValueError('oscillator startup or closed admission failed')
        report['electrical'] = dict(raw_edges=len(raw), minimum_supply_V=min(steady_supply),
            delivered_clock_held_low=True, controller_events=len(records))
        report['status'] = 'startup_pass'
    else:
        report['electrical'] = qualify(rows, records, 'psp')
        report['status'] = 'pass'
    report['samples'] = len(rows)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('oscillator', 'gate', 'oscillator-layout', 'gate-layout', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--startup-only', action='store_true')
    run(parser.parse_args())
