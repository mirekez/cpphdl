#!/usr/bin/env python3
"""Check the connected programmable receive stage in independent generic flows."""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA'), str(SOURCE.parents[1])]
from hardware import configure_cleanup, execute, source_dependencies
from compare import checked_rows, errors, read_cpp, read_spice
from spice_vectors import keep_observed_voltages

HEADER = ['time_s', 'IN', 'OUT', 'BIAS', 'DRIVE', 'FEEDBACK', 'VDD', 'LAST_TAP', 'SELECT_N']
WARMUP = 16e-6
CODES = (4, 5, 6, 7, 8)
GAIN_LIMITS = tuple((.8 * code / (9 - code), 1.2 * code / (9 - code)) for code in CODES)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    checked_rows(rows, len(HEADER), 'receive gain waveform')
    times = [row[0] for row in rows]
    plateaus = []
    for symbol in range(16):
        start, end = WARMUP + (8 * symbol + 6) * 1e-9, WARMUP + (8 * symbol + 7) * 1e-9
        window = rows[bisect_left(times, start):bisect_right(times, end)]
        if len(window) < 10:
            raise ValueError('receive gain waveform misses a symbol aperture')
        plateaus.append([sum(row[i] for row in window) / len(window) for i in range(len(HEADER))])
    x = [row[1] for row in plateaus]
    y = [row[2] - row[3] for row in plateaus]
    mean_x, mean_y = sum(x) / len(x), sum(y) / len(y)
    energy = sum((value - mean_x) ** 2 for value in x)
    if energy < 1e-4:
        raise ValueError('gain stimulus has insufficient input variation')
    gain = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y)) / energy
    offset = mean_y - gain * (mean_x - 1.4)
    residual = max(abs(b - (offset + gain * (a - 1.4))) for a, b in zip(x, y))
    settled_low, settled_high = min(y), max(y)
    transient = [row[2] - row[3] for row in rows]
    overshoot = max(0, max(transient) - settled_high, settled_low - min(transient))
    limits = GAIN_LIMITS[CODES.index(code)]
    checks = dict(
        gain=gain < 0 and limits[0] <= -gain <= limits[1],
        linearity=residual <= .015,
        overshoot=overshoot <= .045,
        dc_offset=abs(offset) <= .15,
        output_headroom=all(.85 <= row[2] <= 2.05 for row in rows),
        bias_headroom=all(1.05 <= row[3] <= 1.75 for row in rows),
        mean_stability=max(row[3] for row in rows) - min(row[3] for row in rows) <= .01,
        drive_headroom=all(1.5 <= row[4] <= 3.25 for row in rows),
        supply=all(3.2 <= row[6] <= 3.35 for row in rows),
        selection_control=all(abs(row[8]) <= .01 for row in rows))
    return dict(checks=checks, gain=gain, fitted_offset_V=offset, maximum_linearity_residual_V=residual,
                overshoot_V=overshoot, output_range_V=[min(row[2] for row in rows), max(row[2] for row in rows)],
                minimum_supply_V=min(row[6] for row in rows), plateau_count=len(plateaus))


def verify(result):
    failed = [name for name, passed in result['checks'].items() if not passed]
    if failed:
        raise ValueError('receive gain purpose failed: ' + ', '.join(failed))


def windowed_deck(text):
    lines, count = text.splitlines(), 0
    for index, line in enumerate(lines):
        if line.startswith('tran '):
            fields = line.split()
            if len(fields) != 6 or fields[3] != '0' or fields[-1] != 'uic':
                raise ValueError('changed receive gain transient interface')
            fields[3] = format(WARMUP, '.17g')
            lines[index] = ' '.join(fields)
            count += 1
    if count != 1:
        raise ValueError('expected one receive gain transient')
    return keep_observed_voltages('\n'.join(lines) + '\n')


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    tracked = [SOURCE, ANALOG / 'VerilogA/compare.py', SOURCE.parents[1] / 'spice_vectors.py', args.bench.resolve()]
    if args.flow == 'veriloga':
        tracked += [args.openvaf.resolve(), args.ngspice.resolve()]
    hashes.update({str(path): digest(path) for path in tracked})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, cases={}, run_directory=str(work),
        scope='electrical gain stage with fixed high-voltage host controls; no integrated AGC, noise, layout or full-PHY proof')
    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)
    def command(label, argv, case, timeout=300):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, stdout=log, stderr=-2, check=True, timeout=timeout)
    save()
    try:
        for code in CODES:
            case = work / str(code)
            case.mkdir()
            command('bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export', code, 20], case)
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('changed receive gain observations')
            else:
                (case / 'bench_window.cir').write_text(windowed_deck((case / 'bench.cir').read_text()))
                command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'], case)
                command('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench_window.cir'], case)
                if re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', (case / 'ngspice.log').read_text(), re.I):
                    raise ValueError('receive gain electrical simulator failed')
                raw = case / 'spice.dat'
                rows = read_spice(raw, HEADER)
            result = contract(rows, code)
            report['cases'][str(code)] = result | dict(waveform=str(raw), waveform_sha256=digest(raw))
            save()
            verify(result)
            print('PASS', args.flow, 'code', code, 'gain', result['gain'], flush=True)
        gains = [-report['cases'][str(code)]['gain'] for code in CODES]
        if any(a >= b for a, b in zip(gains, gains[1:])):
            raise ValueError('receive gain controls are not monotonic')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('receive gain inputs changed during verification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    root = args.work.resolve()
    reports, trajectories = [], []
    for flow in ('cpp', 'veriloga'):
        report = json.loads((root / flow / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(map(str, CODES)):
            raise ValueError('passing independent receive gain flow required')
        if any(digest(path) != value for path, value in report['source_sha256'].items()):
            raise ValueError('stale receive gain input evidence')
        cases = {}
        for code in CODES:
            previous = report['cases'][str(code)]
            raw = Path(previous['waveform'])
            if digest(raw) != previous['waveform_sha256']:
                raise ValueError('changed receive gain waveform')
            rows = read_cpp(raw)[1] if flow == 'cpp' else read_spice(raw, HEADER)
            result = contract(rows, code)
            verify(result)
            if any(previous[key] != value for key, value in result.items()):
                raise ValueError('receive gain absolute results are stale')
            cases[code] = rows
        reports.append(report)
        trajectories.append(cases)
    results = {}
    for code in CODES:
        differences = errors(trajectories[0][code], trajectories[1][code])
        if any(value > .005 for value in differences):
            raise ValueError('receive gain C++/Verilog-A peak difference exceeds 5 mV')
        results[str(code)] = dict(peak_differences_V=dict(zip(HEADER[1:], differences)))
    (root / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=results,
        flow_report_sha256={flow: digest(root / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')
    print('PASS separate absolute checks and direct C++/Verilog-A waveform comparison')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    if args.flow != 'compare' and args.bench is None:
        parser.error('a gain bench is required')
    if args.flow == 'veriloga' and (args.openvaf is None or args.ngspice is None):
        parser.error('Verilog-A requires OpenVAF and ngspice')
    compare(args) if args.flow == 'compare' else run(args)
