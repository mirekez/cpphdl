#!/usr/bin/env python3
"""Check all sixteen physical sampling slices under coupled programmable gain."""
import argparse
import csv
import gzip
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from hardware import configure_cleanup, execute, source_dependencies
from compare import checked_rows as strict_checked_rows, errors as directional_errors, read_cpp as plain_read_cpp
from trace_storage import archive_trace, capture_trace
from programmable_receive_gain import windowed_deck

HEADER = ['time_s', 'PLUS', 'MINUS', 'GAIN_INPUT', 'GAIN_OUTPUT', 'VDD', 'VLOGIC', 'GAIN_BIAS'] + [name + str(slice) for slice in range(16) for name in ('HELD', 'TRACK')]
WARMUP = 16e-6


def checked_rows(rows, width, label):
    # Keep every repeated-time SPICE state for absolute checks and the reverse
    # comparison. Repeated times must never become an ordering repair.
    previous = None
    for row in rows:
        if len(row) != width or not all(math.isfinite(value) for value in row):
            raise ValueError(label + ': nonrectangular or nonfinite sample')
        if previous is not None and row[0] < previous:
            raise ValueError(label + ': backward time')
        previous = row[0]
    if len(rows) < 10:
        raise ValueError(label + ': incomplete trajectory')


def read_spice(path, header):
    with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as source:
        names = next(source).split()
        if len(names) != len(header) or names[0] != 'time':
            raise ValueError('changed array SPICE observation interface')
        rows = [[float(value) for value in line.split()] for line in source if line.strip()]
    checked_rows(rows, len(header), str(path))
    return rows


def read_cpp(path):
    if path.suffix != '.gz':
        return plain_read_cpp(path)
    with gzip.open(path, 'rt', newline='') as source:
        reader = csv.reader(source)
        header = next(reader)
        return header, strict_checked_rows(reader, len(header), str(path))


def errors(native, reference):
    unique = []
    for row in reference:
        if not unique or row[0] != unique[-1][0]:
            unique.append(row)
    # All original reference rows, including equal-time states, are checked
    # against native interpolation. No voltage or time adjustment is applied.
    forward = directional_errors(native, unique)
    backward = directional_errors(reference, native)
    return [max(a, b) for a, b in zip(forward, backward)]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    checked_rows(rows, len(HEADER), 'sixteen-slice programmable receiver')
    times = [row[0] for row in rows]
    if times[0] > WARMUP + 20e-12 or times[-1] < WARMUP + 256e-9 - 1e-15:
        raise ValueError('missing complete array observation window')

    def aperture(start, end):
        window = rows[bisect_left(times, start):bisect_right(times, end)]
        if len(window) < 10:
            raise ValueError('missing sixteen-slice acquisition aperture')
        return window

    def interpolate(time, column):
        right = bisect_left(times, time)
        if right == 0 or right == len(times):
            raise ValueError('missing clock-defined sampling instant')
        left, upper = rows[right - 1], rows[right]
        fraction = (time - left[0]) / (upper[0] - left[0])
        return left[column] + fraction * (upper[column] - left[column])

    plateaus = []
    for symbol in range(32):
        window = aperture(WARMUP + (8 * symbol + 7) * 1e-9, WARMUP + (8 * symbol + 7.8) * 1e-9)
        plateaus.append([sum(row[i] for row in window) / len(window) for i in range(len(HEADER))])
    x = [row[3] - row[7] for row in plateaus]
    y = [row[4] - row[7] for row in plateaus]
    mean_x, mean_y = sum(x) / len(x), sum(y) / len(y)
    energy = sum((value - mean_x) ** 2 for value in x)
    if energy < 1e-4:
        raise ValueError('insufficient differential stimulus')
    gain = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y)) / energy
    residual = max(abs(b - (mean_y + gain * (a - mean_x))) for a, b in zip(x, y))
    intended = code / (9 - code)
    symbols = (0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0)
    termination_error = max(abs(row[1] - row[2] - .06 * (10 - code) / code * symbols[i % 16])
                            for i, row in enumerate(plateaus))
    acquisition, variation, closed_track, held_min, held_max = 0, 0, 0, float('inf'), -float('inf')
    coverage = [0] * 16
    for sample in range(64):
        slice = sample % 16
        close = WARMUP + (4 * sample + 2.5) * 1e-9
        # The reference is fixed at the prescribed track falling-edge midpoint.
        # It cannot follow a later input transition or fit a solver's waveform.
        target = interpolate(close + .05e-9, 4)
        window = aperture(close + .35e-9, close + 1e-9)
        held = [row[8 + 2 * slice] for row in window]
        acquisition = max(acquisition, max(abs(value - target) for value in held))
        variation = max(variation, max(held) - min(held))
        closed_track = max(closed_track, max(abs(row[9 + 2 * slice]) for row in window))
        held_min, held_max = min(held_min, min(held)), max(held_max, max(held))
        coverage[slice] += 1
    checks = dict(gain=gain < 0 and .8 * intended <= -gain <= 1.2 * intended,
        linearity=residual <= .015,
        gain_output_headroom=all(.85 <= row[4] <= 2.05 for row in rows),
        dc_offset=abs(mean_y - gain * mean_x) <= .15,
        bias_headroom=all(1.05 <= row[7] <= 1.9 for row in rows),
        fixed_termination=termination_error <= .010,
        acquisition=acquisition <= .020,
        hold_variation=variation <= .010,
        held_headroom=.85 <= held_min <= held_max <= 2.4,
        track_closed=closed_track <= .010,
        analog_supply=all(3.15 <= row[5] <= 3.35 for row in rows),
        logic_supply=all(1.1 <= row[6] <= 1.25 for row in rows),
        every_slice=coverage == [4] * 16)
    return dict(checks=checks, gain=gain, maximum_linearity_residual_V=residual,
        maximum_termination_error_V=termination_error, maximum_acquisition_error_V=acquisition,
        maximum_hold_variation_V=variation, samples_per_slice=coverage, plateau_count=32)


def verify(result):
    failed = [key for key, value in result['checks'].items() if not value]
    if failed:
        raise ValueError('coupled receiver failed: ' + ', '.join(failed))


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    (root.parent / 'comparison.json').unlink(missing_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    sources = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    tracked = [SOURCE, SOURCE.with_name('programmable_receive_gain.py'), ANALOG / 'VerilogA/compare.py',
               ANALOG / 'VerilogA/trace_storage.py',
               SOURCE.parents[1] / 'spice_vectors.py', args.bench.resolve()]
    if args.flow == 'veriloga':
        tracked += [ANALOG / 'VerilogA/hierarchy.py', ANALOG / 'VerilogA/Driver.cpp', args.driver.resolve()]
        tracked += [args.openvaf.resolve(), args.ngspice.resolve()]
    sources.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=sources,
        run_directory=str(work), fixture_source=str(args.fixture_source.resolve()), scope='fixed selection, sixteen physical 1.5 pF samplers at 250 MS/s; no ADC conversion, AGC, PSP or layout proof')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(text)
        (work / 'checks.json').write_text(text)

    save()
    try:
        for code in args.codes:
            case = work / str(code)
            case.mkdir()
            output = case / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
            with capture_trace(output) as captured:
                with (case / 'bench.log').open('w') as log:
                    execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export', str(code)],
                            stdout=log, stderr=-2, check=True, timeout=7200)
                if args.flow == 'veriloga':
                    (case / 'bench.cir').write_text(windowed_deck((case / 'bench.cir').read_text()))
                    with (case / 'partition.log').open('w') as log:
                        execute([str(args.driver.resolve()), '--partition', str(case / 'design.va'),
                            '--output', str(case), '--experiment', str(case / 'bench.cir')],
                            stdout=log, stderr=-2, check=True, timeout=60, memory_exclusive=False)
                    hierarchy = json.loads((case / 'AnalogExperiment.hierarchy.json').read_text())
                    for master in hierarchy['masters']:
                        with (case / (master + '.log')).open('w') as log:
                            execute([str(args.openvaf.resolve()), master + '.va', '-o', master + '.osdi'],
                                cwd=case, stdout=log, stderr=-2, check=True, timeout=180, memory_exclusive=False)
                    with (case / 'ngspice.log').open('w') as log:
                        execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'],
                            cwd=case, env=dict(os.environ, OMP_THREAD_LIMIT='1'),
                            stdout=log, stderr=-2, check=True, timeout=3600)
            raw = captured.path
            if args.flow == 'cpp':
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('changed programmable receiver observations')
            else:
                rows = read_spice(raw, HEADER)
            result = contract(rows, code)
            result.update(waveform=str(raw), waveform_sha256=digest(raw))
            result['waveform_archive'] = captured.metadata
            result['equal_time_observations_retained'] = sum(a[0] == b[0] for a, b in zip(rows, rows[1:]))
            result['artifact_sha256'] = {str(path): digest(path) for path in case.iterdir()
                if path.is_file() and path.suffix in ('.va', '.cir', '.json', '.osdi')}
            report['cases'][str(code)] = result
            save()
            verify(result)
            print('PASS', args.flow, code, result, flush=True)
        if any(digest(path) != value for path, value in sources.items()):
            raise ValueError('receiver inputs changed during verification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    (args.work / 'comparison.json').unlink(missing_ok=True)
    reports = {flow: json.loads((args.work / flow / 'checks.json').read_text()) for flow in ('cpp', 'veriloga')}
    result = dict(status='running', cases={})
    for flow, report in reports.items():
        if report['status'] != 'pass' or set(report['cases']) != set(map(str, args.codes)):
            raise ValueError('missing independent receiver flow')
        if str(SOURCE) not in report.get('source_sha256', {}):
            raise ValueError('missing receiver checker provenance')
        for path, expected in report['source_sha256'].items():
            if digest(path) != expected:
                raise ValueError('stale receiver source or tool: ' + path)
    if (not reports['cpp'].get('fixture_source')
            or reports['cpp']['fixture_source'] != reports['veriloga'].get('fixture_source')):
        raise ValueError('receiver flows used different class fixtures')
    for code in args.codes:
        waveforms = {}
        for flow, report in reports.items():
            case = report['cases'][str(code)]
            if digest(case['waveform']) != case['waveform_sha256']:
                raise ValueError('changed receiver waveform')
            if any(digest(path) != expected for path, expected in case.get('artifact_sha256', {}).items()):
                raise ValueError('changed receiver experiment or compiled model')
            raw = Path(case['waveform'])
            rows = read_cpp(raw)[1] if flow == 'cpp' else read_spice(raw, HEADER)
            verify(contract(rows, code))
            waveforms[flow] = rows
        peak = dict(zip(HEADER[1:], errors(waveforms['cpp'], waveforms['veriloga'])))
        if max(peak.values()) > .010:
            raise ValueError('coupled receiver waveform difference exceeds 10 mV')
        result['cases'][str(code)] = dict(peak_differences_V=peak)
    result.update(status='pass', flow_report_sha256={flow: digest(args.work / flow / 'checks.json') for flow in reports})
    (args.work / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_suffix('.cpp'))
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--codes', type=int, choices=range(4, 9), nargs='+', default=[4, 8])
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--driver', type=Path)
    args = parser.parse_args()
    if len(args.codes) != len(set(args.codes)):
        parser.error('duplicate gain setting')
    if args.flow != 'compare' and args.bench is None:
        parser.error('flow requires its actual C++ bench')
    if args.flow == 'veriloga' and (args.openvaf is None or args.ngspice is None or args.driver is None):
        parser.error('Verilog-A flow requires the partition driver, OpenVAF and ngspice')
    compare(args) if args.flow == 'compare' else run(args)
