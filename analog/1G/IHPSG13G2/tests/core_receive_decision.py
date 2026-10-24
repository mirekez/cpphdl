#!/usr/bin/env python3
"""Verify a core trial-to-decision path against its 4 ns interface schedule."""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import gzip
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests'), str(SOURCE.parents[1])]
from hardware import configure_cleanup, execute, source_dependencies
from trace_validation import validate_rows
from trace_states import read_states
from trace_storage import capture_trace
from time_grid import windowed_transient, check_observed_grid
from spice_vectors import keep_observed_voltages
from mos_geometry import size_mos_instances
from core_receive_decision_process import process_deck
from core_receive_dac_compare import directional_errors

HEADER = ['time_s', 'IN', 'DAC', 'PRE_PLUS', 'PRE_MINUS', 'EVALUATE', 'CLOCK',
    'POSITIVE', 'NEGATIVE', 'DECISION', 'CORE_DECISION', 'VDD', 'VLOGIC', 'BIAS',
    'CLEAR', 'CLEAR_HV', 'RETURN_LOW']
HEADER += [name + str(i) for i in range(10) for name in ('CONTROL', 'COMPLEMENT', 'LOW', 'LOW_N')]
CODES = (96, 128, 112, 127, 128, 112, 96, 128) * 2
WARMUP, PERIOD, STOP = 8e-6, 4e-9, 8.064e-6


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def compress_native_trace(path):
    """Retain every CSV byte and verify the archive before removing its source."""
    expected = digest(path)
    archive = path.with_suffix(path.suffix + '.gz')
    with path.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as target:
        shutil.copyfileobj(source, target)
    actual = hashlib.sha256()
    with gzip.open(archive, 'rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            actual.update(block)
    if actual.hexdigest() != expected:
        raise ValueError('native waveform compression changed its bytes')
    path.unlink()
    return archive, expected


def contract(rows):
    validate_rows(rows, len(HEADER), 'connected receive decision')
    times = [row[0] for row in rows]
    if times[0] > WARMUP + 20e-12 or abs(times[-1] - STOP) > 1e-14:
        raise ValueError('incomplete connected decision trajectory')
    dac_errors, input_errors, decision_ok, clock_arrivals = [], [], [], []
    for index, code in enumerate(CODES):
        begin = WARMUP + index * PERIOD
        above = (index % 2 == 0) != (index >= 8)
        def window(a, b):
            selected = rows[bisect_left(times, begin + a):bisect_right(times, begin + b)]
            if len(selected) < 20:
                raise ValueError('missing connected decision aperture')
            return selected
        threshold = window(1.5e-9, 2e-9)
        returned = window(3.6e-9, 3.95e-9)
        dac_errors.append(max(abs(r[2] * 256 / r[11] - code) for r in threshold))
        expected_input = 3.3 * (code + (2.5 if above else -2.5)) / 256
        input_errors.append(max(abs(r[1] - expected_input) for r in threshold))
        decision_ok.append(all(r[10] >= .84 if above else r[10] <= .36 for r in returned))
        clock = window(2e-9, 3.6e-9)
        crossings = [r[0] - begin for r in clock if r[6] >= r[11] / 2]
        clock_arrivals.append(min(crossings) if crossings else None)
    exposure = 0.
    for row in rows:
        for i in range(10):
            control, complement, low, low_n = row[17 + 4 * i:21 + 4 * i]
            exposure = max(exposure, abs(control), abs(complement), abs(low), abs(low_n),
                abs(control - complement), abs(control - row[12]), abs(complement - row[12]),
                abs(control - low_n), abs(complement - low))
        # The returning HV transistor protects these core PMOS/inverter nodes.
        exposure = max(exposure, abs(row[16]), abs(row[10]), abs(row[16] - row[12]),
            abs(row[16] - row[10]), abs(row[10] - row[12]))
    checks = dict(dac_settling=max(dac_errors) <= 2, input_boundary=max(input_errors) <= .002,
        returned_decisions=all(decision_ok), evaluation_arrives=all(t is not None for t in clock_arrivals),
        io_supply=all(3 <= r[11] <= 3.35 for r in rows),
        core_supply=all(1.08 <= r[12] <= 1.35 for r in rows),
        bias=all(.8 <= r[13] <= 2.9 for r in rows), core_device_exposure=exposure <= 1.65,
        clear_released=all(abs(r[14]) <= .02 and abs(r[15]) <= .1 for r in rows),
        core_return_range=all(-.1 <= r[10] <= r[12] + .1 for r in rows))
    return dict(checks=checks, codes=list(CODES), decision_pass=decision_ok,
        dac_aperture_errors_counts=dac_errors, maximum_dac_aperture_error_counts=max(dac_errors),
        maximum_input_error_V=max(input_errors), maximum_core_device_exposure_V=exposure,
        evaluation_clock_half_rail_arrival_s=clock_arrivals,
        decision_window_s=[3.6e-9, 3.95e-9], input_distance_counts=2.5,
        scope='driven input, real translated DAC/evaluation/clear and physical core return; sampler and routed controller arrival times are separate integration requirements')


def verify(result):
    failed = [name for name, passed in result['checks'].items() if not passed]
    if failed:
        raise ValueError('connected decision purpose failed: ' + ', '.join(failed))


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    paths = [SOURCE, args.bench.resolve()]
    paths += [SOURCE.parents[1] / name for name in ('core_receive_decision_process.py', 'core_receive_dac_process.py',
        'core_driver_process.py', 'receive_frontend_process.py', 'spice_vectors.py', 'mos_geometry.py')]
    paths += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_validation.py', 'trace_states.py', 'trace_storage.py', 'time_grid.py')]
    if args.flow != 'cpp':
        paths += [args.ngspice.resolve()]
    if args.flow == 'veriloga':
        paths += [args.openvaf.resolve()]
    if args.flow == 'psp':
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths += [models / '.spiceinit']
        paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): digest(p) for p in paths})
    report = dict(status='running', source_sha256=hashes, flow=args.flow, work=str(work),
        corner=args.corner, temperature_C=args.temperature,
        integration=dict(native='BDF2 with event restarts', native_step_s=.125e-12,
            native_final_warmup_s=.2e-6, native_final_warmup_step_s=10e-12,
            spice='Gear maximum order 2',
            spice_maximum_measurement_step_s=.1e-12 if args.flow == 'veriloga' else .5e-12))
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)
    environment = dict(os.environ, OMP_THREAD_LIMIT='1', OMP_NUM_THREADS='1')
    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=environment, stdout=log, stderr=-2,
                check=True, timeout=args.simulation_timeout)
    save()
    try:
        if args.flow == 'cpp':
            (work / 'cpp').mkdir()
            with capture_trace(work / 'cpp/trace.csv') as capture:
                command('bench', [args.bench.resolve(), work, 'run'])
            raw = capture.path
            report['trace_storage'] = capture.metadata
            report['waveform_uncompressed_sha256'] = capture.metadata['original_sha256']
            rows = read_states(raw, HEADER, csv_format=True)
            grid = dict(start_s=WARMUP, stop_s=STOP, measurement_maximum_step_s=.125e-12)
        else:
            command('bench', [args.bench.resolve(), work, 'export'])
            deck = (work / 'bench.cir').read_text()
            if args.flow == 'veriloga':
                command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
            else:
                deck, inventory = process_deck((work / 'design.va').read_text(), deck,
                    models / 'models', args.corner, args.temperature)
                deck, geometry = size_mos_instances(deck)
                (work / 'bindings.json').write_text(json.dumps(inventory | dict(geometry=geometry), indent=2) + '\n')
                environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
            lines = deck.splitlines()
            transients = [i for i, line in enumerate(lines) if line.startswith('tran ')]
            if len(transients) != 1:
                raise ValueError('changed decision transient interface')
            i = transients[0]
            fields = lines[i].split()
            if len(fields) != 6 or fields[3] != '0' or fields[-1] != 'uic':
                raise ValueError('changed decision cold transient')
            fields[3] = str(WARMUP)
            if args.flow == 'veriloga':
                fields[4] = str(.1e-12)
            lines[i] = ' '.join(fields)
            deck, grid = windowed_transient(keep_observed_voltages('\n'.join(lines) + '\n'), 1e-9)
            (work / 'measurement.cir').write_text(deck)
            with capture_trace(work / 'spice.dat'):
                command('ngspice', [args.ngspice.resolve(), *(['-n'] if args.flow == 'veriloga' else []), '-b', 'measurement.cir'])
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', (work / 'ngspice.log').read_text(), re.I):
                raise ValueError('connected decision simulation failed')
            raw = work / 'spice.dat.gz'
            rows = read_states(raw, HEADER)
        report['observed_grid'] = check_observed_grid(rows, grid)
        result = contract(rows)
        report.update(result, waveform=str(raw), waveform_sha256=digest(raw))
        verify(result)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('connected decision verification inputs changed')
        report['status'] = 'pass'
        print('PASS', args.flow, 'connected receive decisions', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    trajectories, receipts = [], {}
    for flow in ('cpp', 'veriloga'):
        path = args.work / flow / 'checks.json'
        report = json.loads(path.read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow:
            raise ValueError('independently passing connected decision flows required')
        if not report.get('source_sha256') or any(digest(p) != h for p, h in report['source_sha256'].items()):
            raise ValueError('stale connected decision evidence')
        raw = Path(report['waveform'])
        if digest(raw) != report['waveform_sha256']:
            raise ValueError('changed connected decision waveform')
        rows = read_states(raw, HEADER, csv_format=flow == 'cpp')
        absolute = contract(rows)
        verify(absolute)
        if any(report.get(k) != v for k, v in absolute.items()):
            raise ValueError('changed decision absolute results')
        trajectories.append(rows)
        receipts[flow] = digest(path)
    forward, native_count = directional_errors(trajectories[0], trajectories[1])
    reverse, spice_count = directional_errors(trajectories[1], trajectories[0])
    maximum = [max(a, b) for a, b in zip(forward, reverse)]
    passed = max(maximum) <= .005
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass' if passed else 'fail',
        tolerance_V=.005, maximum_differences_V=dict(zip(HEADER[1:], maximum)),
        compared_observations=dict(cpp=native_count, veriloga=spice_count), receipt_sha256=receipts,
        checker_sha256={str(p): digest(p) for p in (SOURCE, SOURCE.with_name('core_receive_dac_compare.py'))}), indent=2) + '\n')
    if not passed:
        raise ValueError('connected decision generic waveforms differ by more than 5 mV')
    print('PASS connected decision comparison on both grids')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--simulation-timeout', type=float, default=18000)
    args = parser.parse_args()
    if not math.isfinite(args.simulation_timeout) or args.simulation_timeout <= 0:
        parser.error('simulation timeout must be finite and positive')
    if args.flow != 'compare' and args.bench is None:
        parser.error('compiled decision bench required')
    if args.flow == 'veriloga' and not all((args.ngspice, args.openvaf)):
        parser.error('Verilog-A requires ngspice and OpenVAF')
    if args.flow == 'psp' and not all((args.ngspice, args.pdk)):
        parser.error('PSP requires ngspice and the IHP PDK')
    compare(args) if args.flow == 'compare' else run(args)
