#!/usr/bin/env python3
"""Apply the same electrical FLP contracts independently, then compare solvers."""
import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies

CASES = {f'driver_{load}_{reverse}': ('driver', load, reverse)
         for load, reverse in ((80, 0), (100, 0), (120, 0), (100, 1))}
CASES.update({f'detector_{amplitude}_{reverse}': ('detector', amplitude, reverse)
              for amplitude in (.25, .8, 1.2, 2.2) for reverse in (0, 1)})
CASES['driver_100_0_refined'] = ('driver', 100, 0)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_case(directory, flow):
    with (directory / 'columns.csv').open() as file:
        header = next(csv.reader(file))
    if flow == 'cpp':
        actual, rows = read_cpp(directory / 'cpp/trace.csv')
        if actual != header:
            raise ValueError('FLP observation interface changed')
    else:
        rows = read_spice(directory / 'spice.dat', header)
    if header != ['time_s', 'P', 'N', 'PULSE', 'COMMAND', 'ENABLE', 'VDD', 'POSITIVE', 'NEGATIVE']:
        raise ValueError('unknown FLP observation interface')
    return rows


def contract(rows, mode, parameter, reverse):
    del reverse  # Reversing input polarity must satisfy the identical contract.
    if (not rows or abs(rows[-1][0] - 1e-6) > 1e-14
            or any(not math.isfinite(value) for row in rows for value in row)):
        raise ValueError('incomplete or nonfinite FLP trajectory')
    times = [row[0] for row in rows]
    if any(b <= a or b - a > .251e-9 for a, b in zip(times, times[1:])):
        raise ValueError('FLP trajectory is unordered or insufficiently resolved')

    def at(time, column):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    active = mode == 'driver' or parameter >= .8
    starts = [160, 320, 480, 640, 800]
    ends = [256, 424, 496, 760, 842 if mode == 'driver' else 904]
    edges = []
    for a, b in zip(rows, rows[1:]):
        if a[0] < 100e-9:
            continue
        if (a[3] >= 1.65) != (b[3] >= 1.65):
            edge = a[0] + (1.65 - a[3]) * (b[0] - a[0]) / (b[3] - a[3])
            edges.append([int(b[3] > a[3]), edge])
    if active:
        if len(edges) != 10:
            raise ValueError('pulse detector lost, duplicated or added a pulse')
        for i, (start, end) in enumerate(zip(starts, ends)):
            rising, falling = edges[2 * i:2 * i + 2]
            if rising[0] != 1 or falling[0] != 0:
                raise ValueError('incorrect detector edge order')
            if not (start * 1e-9 <= rising[1] <= (start + 12) * 1e-9
                    and (end - 2) * 1e-9 <= falling[1] <= (end + 12) * 1e-9):
                raise ValueError('pulse detection or disable exceeded its time bound')
    elif edges or max(row[3] for row in rows if row[0] > 100e-9) > .3:
        raise ValueError('subthreshold differential input produced a pulse')
    # Plateau checks are independent of threshold-crossing checks. A simulator
    # cannot pass merely by toggling through 1.65 V with inadequate logic swing.
    for ns in (190, 350, 487, 700, 825):
        value = at(ns * 1e-9, 3)
        if (active and value < 3.0) or (not active and value > .3):
            raise ValueError('detector logic level did not settle')
    for ns in (120, 290, 450, 530, 780, 950):
        if at(ns * 1e-9, 3) > .3:
            raise ValueError('quiet line or disabled driver produced a pulse')
    amplitudes = [at(ns * 1e-9, 1) - at(ns * 1e-9, 2) for ns in (190, 350, 700, 825)]
    if mode == 'driver':
        if min(amplitudes) < 1.8 or max(amplitudes) > 2.5:
            raise ValueError('loaded driver pulse amplitude failed')
        if max(abs(at(ns * 1e-9, 1) - at(ns * 1e-9, 2)) for ns in (870, 980)) > .02:
            raise ValueError('disabled driver retained differential output')
        droop = 3.3 - at(190e-9, 6)
        if not .005 < droop < .15:
            raise ValueError('supply loading is absent or excessive')
    if min(row[6] for row in rows if row[0] > 100e-9) < 3.1:
        raise ValueError('I/O supply collapsed')
    probes = [[at(ns * 1e-9, c) for c in (1, 2, 3, 6)]
              for ns in (120, 190, 290, 350, 450, 487, 530, 700, 780, 825, 870, 950, 980)]
    return dict(edges=edges, plateau_probes_V=probes, differential_amplitudes_V=amplitudes,
                checks=dict(pulse_count_and_timing=True, settled_levels=True,
                            polarity_and_rejection=True, loaded_supply=True))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.parent / 'flp_frontend.cpp', SOURCE.parent / 'flp_frontend_receiver.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes[str(SOURCE)] = digest(SOURCE)
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    binaries[str(args.decoder.resolve())] = digest(args.decoder.resolve())
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path.resolve()) for path in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries, cases={},
                  scope='generic MOS FLP electrical interface; no process, cable-mask or full-page qualification')
    for name, parameters in CASES.items():
        case = work / name
        case.mkdir(exist_ok=True)
        with (case / 'bench.log').open('w') as log:
            execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                     *map(str, parameters), '8' if name.endswith('_refined') else '4'],
                    stdout=log, stderr=-2, check=True, timeout=60)
        if args.flow == 'veriloga':
            for label, command in [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                   ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]:
                with (case / (label + '.log')).open('w') as log:
                    execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=120)
        rows = read_case(case, args.flow)
        result = contract(rows, *parameters)
        with (case / 'detected.csv').open('w') as file:
            writer = csv.writer(file)
            writer.writerow(['time_s', 'pulse_V'])
            writer.writerows((row[0], row[3]) for row in rows)
        with (case / 'decode.log').open('w') as log:
            execute([str(args.decoder.resolve()), str(case / 'detected.csv'),
                     str(int(parameters[0] == 'driver' or parameters[1] >= .8))],
                    stdout=log, stderr=-2, check=True, timeout=90)
        result['decoder'] = dict(log_sha256=digest(case / 'decode.log'),
                                 template_sha256=digest(case / 'detected.csv'),
                                 expected_pages=32 if parameters[0] == 'driver' or parameters[1] >= .8 else 0,
                                 sampling_phases=8, reference_clocks=8001280)
        trace = case / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
        result.update(waveform=str(trace), waveform_sha256=digest(trace))
        report['cases'][name] = result
        print(name + ': pass', flush=True)
    first, second = [report['cases'][name] for name in ('driver_100_0', 'driver_100_0_refined')]
    delta = [x - y for a, b in zip(first['plateau_probes_V'], second['plateau_probes_V']) for x, y in zip(a, b)]
    refinement_rms = math.sqrt(sum(x * x for x in delta) / len(delta))
    edge_difference = max(abs(x[1] - y[1]) for x, y in zip(first['edges'], second['edges']))
    if refinement_rms > .005 or edge_difference > .25e-9:
        raise ValueError('FLP step refinement did not meet voltage/timing convergence limits')
    report['refinement'] = dict(plateau_rms_difference_V=refinement_rms,
                                maximum_edge_difference_s=edge_difference,
                                step_s=.25e-9 / 4, refined_step_s=.25e-9 / 8)
    for path, value in (hashes | binaries).items():
        if digest(Path(path)) != value:
            raise ValueError('FLP source or binary changed during validation')
    report['status'] = 'pass'
    (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    (args.work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        work = args.work.resolve() / flow
        report = json.loads((work / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both independent FLP flows must pass first')
        for path, value in (report['source_sha256'] | report['binary_sha256']).items():
            if digest(Path(path)) != value:
                raise ValueError('stale FLP flow input')
        for name, parameters in CASES.items():
            old = report['cases'][name]
            if digest(Path(old['waveform'])) != old['waveform_sha256']:
                raise ValueError('FLP waveform changed after checking')
            current = contract(read_case(work / name, flow), *parameters)
            if any(old[key] != value for key, value in current.items()):
                raise ValueError('FLP absolute check differs on repetition')
            decoder = old['decoder']
            case = work / name
            expected = f"PASS 8001280 reference clocks, {decoder['expected_pages']} pages, eight sampling phases and physical short-pulse rejection"
            if (digest(case / 'decode.log') != decoder['log_sha256']
                    or digest(case / 'detected.csv') != decoder['template_sha256']
                    or (case / 'decode.log').read_text().strip() != expected):
                raise ValueError('FLP physical-template decoder evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('FLP flows did not test identical sources')
    cases = {}
    for name in CASES:
        a, b = [report['cases'][name] for report in reports]
        if len(a['edges']) != len(b['edges']) or any(x[0] != y[0] for x, y in zip(a['edges'], b['edges'])):
            raise ValueError('FLP edge sequence differs between solvers')
        timing = max((abs(x[1] - y[1]) for x, y in zip(a['edges'], b['edges'])), default=0)
        delta = [x - y for first, second in zip(a['plateau_probes_V'], b['plateau_probes_V'])
                 for x, y in zip(first, second)]
        rms = math.sqrt(sum(x * x for x in delta) / len(delta))
        if timing > .5e-9 or rms > .02:
            raise ValueError('FLP solvers disagree: ' + name)
        cases[name] = dict(maximum_edge_difference_s=timing, plateau_rms_difference_V=rms)
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=cases,
        limits=dict(maximum_edge_difference_s=.5e-9, plateau_rms_difference_V=.02)), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--decoder', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
