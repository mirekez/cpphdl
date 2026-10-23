#!/usr/bin/env python3
"""Check the 2.8 V input interface and loaded receive path in both solvers."""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from compare import read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies
from receive_frontend import dc_transfer


def shifted(gate):
    overdrive = max(0, gate - .7)
    term = 2 / 500 / .05
    return 2 * overdrive ** 2 / (2 * overdrive + term + math.sqrt(term ** 2 + 4 * overdrive * term))


def contract(rows, count, step):
    width = 9 + count
    stop = (12 + 8 * count) * 1e-9
    if (not rows or not 0 <= rows[0][0] <= step * 1e-12 or abs(rows[-1][0] - stop) > 1e-15
            or any(len(r) != width or not all(math.isfinite(v) for v in r) for r in rows)):
        raise ValueError('incomplete/nonfinite MDI receive trajectory')
    times = [r[0] for r in rows]
    if any(not 0 < b - a <= step * 1.01e-12 for a, b in zip(times, times[1:])):
        raise ValueError('unordered/undersampled MDI receive waveform')

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    errors = dict(input=[], shift=[], drain=[], buffer=[], acquisition=[], hold=[])
    samples = []
    for index in range(2 * count):
        time = (11.8 + 4 * index) * 1e-9
        delta = .12 * ((index * 3) % 5 - 2)
        plus, minus, supply = [at(time, col) for col in (1, 2, 5)]
        p, m = shifted(plus), shifted(minus)
        drain, target = dc_transfer(p, m, supply)
        errors['input'] += [abs(plus - (2.8 + delta / 2)), abs(minus - (2.8 - delta / 2))]
        errors['shift'] += [abs(at(time, 7 + count) - p), abs(at(time, 8 + count) - m)]
        errors['drain'].append(abs(at(time, 3) - drain))
        errors['buffer'].append(abs(at(time, 4) - target))
        held = at(time, 7 + index % count)
        samples.append(held)
        errors['acquisition'].append(abs(held - target))
        end = ((8 + 4 * (index + count)) * 1e-9 if index < count else stop) - .1e-9
        errors['hold'] += [abs(row[7 + index % count] - held) for row in rows if time <= row[0] <= end]
    maxima = {name: max(values, default=0) for name, values in errors.items()}
    limits = dict(input=.002, shift=.002, drain=.02, buffer=.02, acquisition=.02, hold=.02)
    if any(maxima[name] >= limits[name] for name in maxima):
        raise ValueError('MDI delivery contract failed: ' + str(maxima))
    steady = [r for r in rows if r[0] >= 8e-9]
    if not all(3.1 < r[5] < 3.3 and 1.05 < r[6] < 1.21 and .3 < r[4] < r[5] - .3
               and 1.4 < r[7 + count] < 2 and 1.4 < r[8 + count] < 2 for r in steady):
        raise ValueError('MDI supply/common-mode/headroom contract failed')
    probes = [[at((8 + .1 * i) * 1e-9, col) for col in range(1, width)] for i in range(80 * count)]
    return dict(maximum_error_V=maxima, sample_V=samples, probes_V=probes)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(p.resolve()): digest(p) for p in (SOURCE, ANALOG / '1G/tests/receive_frontend.py',
        ANALOG / 'VerilogA/compare.py', args.bench, args.openvaf, args.ngspice)})
    report = dict(status='running', cases={}, source_sha256=hashes,
        scope='generic MOS common-mode interface, amplifier, shared buffer and sampling; no PSP, MDI compliance or layout proof')
    try:
        for name, count, step in (('one', 1, 10), ('four', 4, 10), ('refined', 4, 5)):
            case = work / name
            case.mkdir(exist_ok=True)
            def command(label, argv, timeout=1800):
                with (case / (label + '.log')).open('w') as log:
                    execute(list(map(str, argv)), cwd=case, stdout=log, stderr=-2, timeout=timeout, check=True)
            command('cpp', [args.bench.resolve(), case, 'run', count, '.05', step])
            command('export', [args.bench.resolve(), case, 'export', count, '.05', step], 60)
            command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'], 180)
            command('spice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])
            header, cpp = read_cpp(case / 'cpp/trace.csv')
            expected = ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC'] + [
                'HELD' + str(i) for i in range(count)] + ['SHIFT_P', 'SHIFT_N']
            if header != expected:
                raise ValueError('changed MDI observations')
            va = read_spice(case / 'spice.dat', expected)
            flows = {flow: contract(rows, count, step) for flow, rows in (('cpp', cpp), ('veriloga', va))}
            difference = max(abs(a - b) for r, s in zip(flows['cpp']['probes_V'], flows['veriloga']['probes_V'])
                             for a, b in zip(r, s))
            if difference > .02:
                raise ValueError('independently passing MDI solvers disagree: ' + str(difference))
            report['cases'][name] = dict(flows=flows, maximum_solver_difference_V=difference,
                artifact_sha256={str(p): digest(p) for p in (case / 'cpp/trace.csv', case / 'design.va',
                    case / 'bench.cir', case / 'spice.dat')})
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        refinement = {}
        for flow in ('cpp', 'veriloga'):
            a, b = [report['cases'][name]['flows'][flow]['probes_V'] for name in ('four', 'refined')]
            refinement[flow] = max(abs(x - y) for r, s in zip(a, b) for x, y in zip(r, s))
        if max(refinement.values()) > .02:
            raise ValueError('MDI refinement failed: ' + str(refinement))
        report['maximum_refinement_V'] = refinement
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('MDI qualification sources changed')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--openvaf', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
