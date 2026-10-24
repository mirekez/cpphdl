#!/usr/bin/env python3
"""Check MDI conditioning against independent loaded nodal equations."""
import argparse
import bisect
import json
import math
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from trace_states import read_states
from trace_storage import archive_trace, capture_trace
from hardware import configure_cleanup, execute, source_dependencies
from transmit_driver import digest as path_digest

HEADER = ['time_s', 'P', 'N', 'PLUS', 'MINUS', 'COMMON', 'VDD', 'COMMAND_P', 'COMMAND_N']


def digest(path):
    return path_digest(Path(path))


def commands(index):
    if index >= 625:
        common = 3.3 if index in (625, 627) else 1.65
        return common, common
    value = lambda s: -.125 * (3 * (s // 5 - 2) + s % 5 - 2)
    difference = value(index % 25) + value(index // 25)
    return 2.3 + difference / 2, 2.3 - difference / 2


def oracle(p, n):
    # Six unknown node voltages; solve the independently stated resistor
    # network, including both source impedances and supply droop.
    matrix = [[0.] * 7 for _ in range(6)]
    for a, b, resistance in ((0, 2, 2000), (1, 3, 2000), (2, 3, 100),
                             (2, 4, 330), (3, 4, 330), (4, 5, 1000)):
        g = 1 / resistance
        matrix[a][a] += g
        matrix[b][b] += g
        matrix[a][b] -= g
        matrix[b][a] -= g
    for a, resistance, voltage in ((0, 25, p), (1, 25, n), (4, 1000, 0), (5, 1, 3.3)):
        matrix[a][a] += 1 / resistance
        matrix[a][6] += voltage / resistance
    for column in range(6):
        pivot = max(range(column, 6), key=lambda row: abs(matrix[row][column]))
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        scale = matrix[column][column]
        if abs(scale) < 1e-12:
            raise ValueError('singular independent input-network oracle')
        matrix[column] = [v / scale for v in matrix[column]]
        for row in range(6):
            if row != column:
                scale = matrix[row][column]
                matrix[row] = [a - scale * b for a, b in zip(matrix[row], matrix[column])]
    return [row[6] for row in matrix]


def common_mode_reference():
    # Differential charge has settled in the checking apertures. Common-mode
    # settling has three states: both 330 fF input loads, 2 pF reference storage
    # and 20 pF supply storage. Integrate these independently with 1 ps RK4.
    dc = oracle(2.3, 2.3)
    state, previous, result = [dc[2], dc[4], dc[5]], 2.3, {}
    h = 1e-12
    for index in range(625, 629):
        following = commands(index)[0]

        def derivative(t, s):
            source = previous + (following - previous) * min(1., t / .1e-9)
            x, reference, rail = s
            return [((source - x) / 1012.5 + (reference - x) / 165) / 660e-15,
                    ((x - reference) / 165 + (rail - reference) / 1000 - reference / 1000) / 2e-12,
                    ((3.3 - rail) + (reference - rail) / 1000) / 20e-12]

        states = [state[:]]
        for step in range(8000):
            t = step * h
            a = derivative(t, state)
            b = derivative(t + h / 2, [v + h * k / 2 for v, k in zip(state, a)])
            c = derivative(t + h / 2, [v + h * k / 2 for v, k in zip(state, b)])
            d = derivative(t + h, [v + h * k for v, k in zip(state, c)])
            state = [v + h * (p + 2 * q + 2 * r + s) / 6 for v, p, q, r, s in zip(state, a, b, c, d)]
            states.append(state[:])
        result[index] = states
        previous = following
    return result


def contract(rows):
    if rows[0][0] > 20e-12 or abs(rows[-1][0] - 5.132e-6) > 1e-14:
        raise ValueError('incomplete input-network trajectory')
    times = [row[0] for row in rows]
    maximum, plateaus, common_reference = 0., [], common_mode_reference()
    for index in range(629):
        p, n = commands(index)
        expected = oracle(p, n)
        start = 100e-9 + (index * 8 + 7) * 1e-9
        window = rows[bisect.bisect_left(times, start):bisect.bisect_right(times, start + .8e-9)]
        if len(window) < 10:
            raise ValueError('missing MDI loaded-settling aperture')
        for row in window:
            if abs(row[7] - p) > 1e-12 or abs(row[8] - n) > 1e-12:
                raise ValueError('wrong MDI intent stimulus')
            target = expected
            if index >= 625:
                relative = row[0] - (100e-9 + index * 8e-9)
                x, reference, rail = common_reference[index][round(relative / 1e-12)]
                mdi = p - (p - x) * 25 / 2025
                target = [mdi, mdi, x, x, reference, rail]
            maximum = max(maximum, max(abs(a - b) for a, b in zip(row[1:7], target)))
        plateaus.append([sum(row[c] for row in window) / len(window) for c in range(1, 7)])
    if maximum > .0001:
        raise ValueError('loaded input conditioning differs from its independent network by over 0.1 mV')
    active = plateaus[:625]
    if max(abs(row[2] - row[3]) for row in active) > .045:
        raise ValueError('conditioned differential input exceeds 45 mV')
    if not all(1.85 < (row[2] + row[3]) / 2 < 1.95 for row in active):
        raise ValueError('conditioned common mode outside intended active range')
    # The line is measurably loaded: a directed voltage transfer would hide it.
    if max(abs(row[0] - commands(i)[0]) for i, row in enumerate(active)) < .005:
        raise ValueError('input-network loading did not propagate back to the MDI source')
    return dict(maximum_nodal_error_V=maximum, plateaus_V=plateaus,
                stimulus_states=629, includes_all_625_current_history_combinations=True,
                scope='loaded resistor input conditioning; no transistor receiver, pad, cable or ADC qualification')


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    sources = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    tools = [args.bench.resolve(), SOURCE, ANALOG / 'VerilogA/trace_states.py',
             ANALOG / 'VerilogA/trace_codec.py', ANALOG / 'VerilogA/trace_storage.py']
    if args.flow == 'veriloga':
        tools += [args.openvaf.resolve(), args.ngspice.resolve()]
    sources |= {str(p): digest(p) for p in tools}
    report = dict(status='running', source_sha256=sources, flow=args.flow, cases={})
    try:
        for step in (20, 10):
            case = Path(tempfile.mkdtemp(prefix=f'{step}ps-', dir=root))
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export', str(step)],
                        stdout=log, stderr=-2, check=True, timeout=600)
            if args.flow == 'cpp':
                wave, archive = archive_trace(case / 'cpp/trace.csv')
            else:
                with (case / 'compile.log').open('w') as log:
                    execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                            stdout=log, stderr=-2, check=True, timeout=180)
                with capture_trace(case / 'spice.dat') as captured, (case / 'ngspice.log').open('w') as log:
                    execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                            env=dict(os.environ, OMP_THREAD_LIMIT='1'),
                            stdout=log, stderr=-2, check=True, timeout=300)
                wave, archive = captured.path, captured.metadata
            result = contract(read_states(wave, HEADER, csv_format=args.flow == 'cpp'))
            result.update(waveform=str(wave), waveform_sha256=digest(wave), archive=archive)
            report['cases'][str(step)] = result
        a, b = [report['cases'][s]['plateaus_V'] for s in ('20', '10')]
        report['maximum_refinement_difference_V'] = max(abs(x - y) for p, q in zip(a, b) for x, y in zip(p, q))
        if report['maximum_refinement_difference_V'] > 20e-6:
            raise ValueError('input-network refinement exceeds 20 uV')
        if any(digest(p) != value for p, value in sources.items()):
            raise ValueError('input-network sources changed during verification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (root / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    reports = [json.loads((args.work / flow / 'checks.json').read_text()) for flow in ('cpp', 'veriloga')]
    for flow, report in zip(('cpp', 'veriloga'), reports):
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != {'20', '10'}:
            raise ValueError('both input-network flows must independently pass')
        if any(digest(p) != sha for p, sha in report['source_sha256'].items()):
            raise ValueError('stale input-network source or tool')
        for result in report['cases'].values():
            if digest(result['waveform']) != result['waveform_sha256']:
                raise ValueError('changed input-network trajectory')
            if contract(read_states(result['waveform'], HEADER, csv_format=flow == 'cpp'))['plateaus_V'] != result['plateaus_V']:
                raise ValueError('input-network checking evidence changed')
    maximum = max(abs(a - b) for step in ('20', '10')
                  for p, q in zip(*(r['cases'][step]['plateaus_V'] for r in reports)) for a, b in zip(p, q))
    if maximum > 20e-6:
        raise ValueError('independent input-network simulators differ by over 20 uV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', maximum_difference_V=maximum), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    for name in ('bench', 'openvaf', 'ngspice'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
