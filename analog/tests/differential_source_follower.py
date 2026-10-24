#!/usr/bin/env python3
"""Check differential polarity, common mode, KCL and load-dependent delivery."""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'VerilogA'), str(ROOT / '1G/tests')]
from compare import read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies
from source_follower import settled

HEADER = ['time_s', 'PLUS', 'MINUS', 'OUTP', 'OUTN', 'VDD', 'COMMAND_P', 'COMMAND_N']
LEVELS = [code for first in range(5) for second in range(5) for code in (first, second)]


def contract(rows, supply, return_load):
    if (not rows or not 0 <= rows[0][0] <= 5e-12 or abs(rows[-1][0] - 204e-9) > 1e-15
            or any(len(r) != len(HEADER) or not all(math.isfinite(v) for v in r) for r in rows)):
        raise ValueError('incomplete/nonfinite differential follower trace')
    times = [r[0] for r in rows]
    if any(not 0 < b - a <= 5.01e-12 for a, b in zip(times, times[1:])):
        raise ValueError('unordered/undersampled differential follower trace')

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    delivery, dc, input_errors, differential, common = [], [], [], [], []
    for index, code in enumerate(LEVELS):
        start = (4 + 4 * index) * 1e-9
        target = [settled(2.8 + sign * .06 * (code - 2), return_load) for sign in (1, -1)]
        for column, expected in zip((3, 4), target):
            delivery.append(abs(at(start + 1.5e-9, column) - expected))
            dc.append(abs(at(start + 3.8e-9, column) - expected))
        p, n = at(start + 3.8e-9, 3), at(start + 3.8e-9, 4)
        differential.append(p - n)
        common.append((p + n) / 2)
        input_errors += [abs(at(start + 3.8e-9, 1) - (2.8 + .06 * (code - 2))),
                         abs(at(start + 3.8e-9, 2) - (2.8 - .06 * (code - 2)))]
    steady = [r for r in rows if r[0] >= 4e-9]
    checks = dict(delivery=max(delivery) < .02, dc=max(dc) < .001, input=max(input_errors) < .001,
        common_mode=all(1.1 < v < 2.1 for v in common),
        polarity=all(abs(v) < .001 if code == 2 else v * (code - 2) > 0 for v, code in zip(differential, LEVELS)),
        gain=all(.4 < v / (.12 * (code - 2)) < 1 for v, code in zip(differential, LEVELS) if code != 2),
        supply=all(.85 * supply < r[5] < supply - .001 for r in steady),
        headroom=all(.2 < r[col] < r[5] - .3 for r in steady for col in (3, 4)),
        input_charge=max(abs(r[1] - r[6]) + abs(r[2] - r[7]) for r in steady) > 1e-5)
    return dict(checks=checks, maximum_delivery_V=max(delivery), maximum_dc_V=max(dc),
        common_mode_V=common, differential_V=differential,
        probes_V=[[at((4 + .05 * i) * 1e-9, col) for col in (1, 2, 3, 4, 5)] for i in range(4000)])


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    hashes = source_dependencies([Path(__file__).with_suffix('.cpp')], ROOT.parent, args.cxx,
        ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')])
    hashes.update({str(p.resolve()): digest(p) for p in (Path(__file__), ROOT / '1G/tests/source_follower.py',
        ROOT / 'VerilogA/compare.py', args.bench, args.openvaf, args.ngspice)})
    report = dict(status='running', cases={}, source_sha256=hashes)
    cases = [(str(v), v, .15, 10000, 5) for v in (2.97, 3.3, 3.63)] + [
        ('loaded', 3.3, 4, 10000, 5), ('resistive', 3.3, .15, 100, 5),
        ('overload', 3.3, 40, 10000, 5), ('refined', 3.3, 4, 10000, 2.5)]
    try:
        for name, supply, load, return_load, step in cases:
            case = work / name
            case.mkdir(exist_ok=True)
            def command(label, argv):
                with (case / (label + '.log')).open('w') as log:
                    # Two MOS devices give a small, fixed compilation footprint;
                    # this unit test can run beside the routed-module job.
                    execute(list(map(str, argv)), cwd=case, stdout=log, stderr=-2, check=True, timeout=300,
                            memory_exclusive=False if label == 'compile' else None)
            command('cpp', [args.bench.resolve(), case, 'run', supply, load, return_load, step])
            command('export', [args.bench.resolve(), case, 'export', supply, load, return_load, step])
            command('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
            command('spice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])
            header, cpp = read_cpp(case / 'cpp/trace.csv')
            if header != HEADER:
                raise ValueError('changed differential follower columns')
            va = read_spice(case / 'spice.dat', HEADER)
            flows = {flow: contract(rows, supply, return_load) for flow, rows in (('cpp', cpp), ('veriloga', va))}
            for flow, result in flows.items():
                for check, passed in result['checks'].items():
                    # With excessive capacitance the output is deliberately
                    # unsettled. Settled zero/polarity/gain checks therefore
                    # apply to the rated loads, while the overload must fail
                    # delivery and retain input, supply and headroom bounds.
                    if name == 'overload' and check in ('dc', 'polarity', 'gain'):
                        continue
                    if passed != (name != 'overload' or check != 'delivery'):
                        raise ValueError(f'{name}/{flow} differential follower contract: {result["checks"]}')
            difference = max(abs(a - b) for r, s in zip(flows['cpp']['probes_V'], flows['veriloga']['probes_V'])
                             for a, b in zip(r, s))
            if difference > .01:
                raise ValueError('differential follower solver disagreement: ' + str(difference))
            report['cases'][name] = dict(flows=flows, maximum_solver_difference_V=difference,
                artifact_sha256={str(p): digest(p) for p in (case / 'cpp/trace.csv', case / 'design.va',
                    case / 'bench.cir', case / 'spice.dat')})
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        refinement = {flow: max(abs(a - b) for r, s in zip(report['cases']['loaded']['flows'][flow]['probes_V'],
            report['cases']['refined']['flows'][flow]['probes_V']) for a, b in zip(r, s)) for flow in ('cpp', 'veriloga')}
        if max(refinement.values()) > .01:
            raise ValueError('differential follower refinement failed: ' + str(refinement))
        report['maximum_refinement_V'] = refinement
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('differential follower sources changed')
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
