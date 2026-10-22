#!/usr/bin/env python3
"""Check coupled differential gain and receiver acquisition in both equation solvers."""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from hardware import configure_cleanup, execute, source_dependencies
from compare import checked_rows, errors, read_cpp, read_spice
from programmable_receive_gain import windowed_deck

HEADER = ['time_s', 'PLUS', 'MINUS', 'GAIN_INPUT', 'GAIN_OUTPUT', 'HELD', 'TRACK', 'VDD', 'VLOGIC', 'GAIN_BIAS']
WARMUP = 16e-6


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contract(rows, code):
    checked_rows(rows, len(HEADER), 'coupled programmable receiver')
    times = [row[0] for row in rows]
    plateaus = []
    aperture_rows = []
    for symbol in range(16):
        start, end = WARMUP + (8 * symbol + 7) * 1e-9, WARMUP + (8 * symbol + 7.8) * 1e-9
        window = rows[bisect_left(times, start):bisect_right(times, end)]
        if len(window) < 10:
            raise ValueError('missing receiver acquisition aperture')
        aperture_rows += window
        plateaus.append([sum(row[i] for row in window) / len(window) for i in range(len(HEADER))])
    x = [row[3] - row[9] for row in plateaus]
    y = [row[4] - row[9] for row in plateaus]
    mean_x, mean_y = sum(x) / len(x), sum(y) / len(y)
    energy = sum((value - mean_x) ** 2 for value in x)
    if energy < 1e-4:
        raise ValueError('insufficient differential stimulus')
    gain = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y)) / energy
    residual = max(abs(b - (mean_y + gain * (a - mean_x))) for a, b in zip(x, y))
    acquisition = max(abs(row[5] - row[4]) for row in aperture_rows)
    intended = code / (9 - code)
    symbols = (0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0)
    # Two 50 ohm source impedances drive the fixed 100 ohm termination.
    termination_error = max(abs(row[1] - row[2] - .06 * (10 - code) / code * symbol)
                            for row, symbol in zip(plateaus, symbols))
    checks = dict(gain=gain < 0 and .8 * intended <= -gain <= 1.2 * intended,
        linearity=residual <= .015,
        gain_output_headroom=all(.85 <= row[4] <= 2.05 for row in rows),
        dc_offset=abs(mean_y - gain * mean_x) <= .15,
        bias_headroom=all(1.05 <= row[9] <= 1.9 for row in rows),
        fixed_termination=termination_error <= .010,
        acquisition=acquisition <= .020,
        held_headroom=all(.85 <= row[5] <= 2.4 for row in plateaus),
        track_closed=all(abs(row[6]) <= .010 for row in plateaus),
        analog_supply=all(3.15 <= row[7] <= 3.35 for row in rows),
        logic_supply=all(1.1 <= row[8] <= 1.25 for row in rows))
    return dict(checks=checks, gain=gain, maximum_linearity_residual_V=residual, maximum_termination_error_V=termination_error,
                maximum_acquisition_error_V=acquisition, plateau_count=16)


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
               SOURCE.parents[1] / 'spice_vectors.py', args.bench.resolve()]
    if args.flow == 'veriloga':
        tracked += [args.openvaf.resolve(), args.ngspice.resolve()]
    sources.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=sources,
        run_directory=str(work), fixture_source=str(args.fixture_source.resolve()), scope='fixed selection, one physical sampler; no ADC conversion, AGC, PSP or layout proof')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(text)
        (work / 'checks.json').write_text(text)

    save()
    try:
        for code in args.codes:
            case = work / str(code)
            case.mkdir()
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export', str(code)],
                        stdout=log, stderr=-2, check=True, timeout=1800)
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('changed programmable receiver observations')
            else:
                (case / 'bench.cir').write_text(windowed_deck((case / 'bench.cir').read_text()))
                for label, command in [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                        ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]:
                    with (case / (label + '.log')).open('w') as log:
                        # The fixed single-slice compile was measured below 300 MiB.
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=1800,
                                memory_exclusive=False if label == 'compile' else None)
                raw = case / 'spice.dat'
                rows = read_spice(raw, HEADER)
            result = contract(rows, code)
            result.update(waveform=str(raw), waveform_sha256=digest(raw))
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
    for code in args.codes:
        waveforms = {}
        for flow, report in reports.items():
            case = report['cases'][str(code)]
            if digest(case['waveform']) != case['waveform_sha256']:
                raise ValueError('changed receiver waveform')
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
    parser.add_argument('--codes', type=int, choices=range(4, 9), nargs='+', default=list(range(4, 9)))
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    if len(args.codes) != len(set(args.codes)):
        parser.error('duplicate gain setting')
    if args.flow != 'compare' and args.bench is None:
        parser.error('flow requires its actual C++ bench')
    if args.flow == 'veriloga' and (args.openvaf is None or args.ngspice is None):
        parser.error('Verilog-A flow requires OpenVAF and ngspice')
    compare(args) if args.flow == 'compare' else run(args)
