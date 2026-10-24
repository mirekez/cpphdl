#!/usr/bin/env python3
"""Check the physical main/history driver independently in each solver."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows, read_cpp, read_spice
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit
from hardware import configure_cleanup, execute, source_dependencies

CASES = {'nominal': (100, False, True), 'low_load': (80, False, True),
         'high_load': (120, False, True), 'open': (0, False, True),
         'refined': (100, True, True), 'unfiltered': (100, False, False)}
HEADER = ['time_s'] + [f'G{bank}_{bit}' for bank in range(4) for bit in range(4)] + [
    'P', 'N', 'DIFF', 'BIAS', 'VDD', 'SUPPLY_DROP']


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def controls(path):
    with path.open() as file:
        words = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file)]
    previous, fault = 2, False
    for i, word in enumerate(words):
        valid = not word['reset'] and word['enable'] and not fault and word['code'] <= 4
        if word['previous'] != previous or word['index'] != i:
            raise ValueError('driver history/stimulus interface changed')
        masks = [0] * 4
        if valid:
            for bit in range(4):
                masks[0] |= int(bit < word['code']) << bit
                masks[1] |= int(bit >= word['code']) << bit
                masks[2] |= int(bit < previous) << bit
                masks[3] |= int(bit >= previous) << bit
        if word['enable'] and word['code'] > 4:
            fault = True
        if word['reset']:
            fault = False
        if ([word[key] for key in ('main_p', 'main_n', 'history_p', 'history_n')] != masks
                or word['valid'] != valid or word['fault'] != fault):
            raise ValueError('physical driver stimulus disagrees with independent controller oracle')
        previous = word['code'] if valid else 2
    if len(words) < 100:
        raise ValueError('incomplete transmit stimulus')
    return words


def read_saved(path, flow):
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != HEADER:
                raise ValueError('stored driver observation interface changed')
        else:
            names = next(file).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('stored driver SPICE interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def contract(rows, words, interface):
    stop, ui, startup = [interface[key] for key in ('stop_s', 'ui_s', 'startup_s')]
    if abs(rows[-1][0] - stop) > 1e-14 or any(b[0] - a[0] > ui / 500 for a, b in zip(rows, rows[1:])):
        raise ValueError('incomplete or insufficiently resolved driver trajectory')
    times = [row[0] for row in rows]

    def at(time, col):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[col] + (b[col] - a[col]) * (time - a[0]) / (b[0] - a[0])

    load = interface['load_ohm']
    scale = 2 if load == 0 else .04 / (.02 + 2 / load)
    plateau, target, active_currents, inactive_currents, levels = [], [], [], [], set()
    for i, word in enumerate(words):
        if i < interface['warm_words']:
            continue
        t = startup + (i + .8) * ui
        masks = [word[key] for key in ('main_p', 'main_n', 'history_p', 'history_n')]
        for bank, mask in enumerate(masks):
            for bit in range(4):
                gate = at(t, 1 + 4 * bank + bit)
                if (gate < 3.0 if mask & (1 << bit) else gate > .3):
                    raise ValueError('a physical steering gate failed to settle')
        weighted = 3 * (2 - word['code']) + (2 - word['previous']) if word['valid'] else 0
        if word['valid']:
            levels.add(weighted)
        expected = .125 * weighted * scale
        measured = at(t, 19)
        if abs(at(t, 17) - at(t, 18) - measured) > 1e-8:
            raise ValueError('differential observation is inconsistent with physical pins')
        plateau.append([at(t, column) for column in (17, 18, 19, 20, 21, 22)])
        target.append(expected)
        (active_currents if word['valid'] else inactive_currents).append(at(t, 22) / .25)
    if levels != set(range(-8, 9)):
        raise ValueError('physical stimulus did not exercise all seventeen weighted levels')
    steady = [row for row in rows if row[0] >= startup + interface['warm_words'] * ui]
    ripple = max(row[20] for row in steady) - min(row[20] for row in steady)
    error = max(abs(row[2] - expected) for row, expected in zip(plateau, target))
    checks = dict(finite_supply=min(row[21] for row in steady) > 3.05,
                  physical_nodes_bounded=min(min(row[17:19]) for row in steady) > -.1,
                  seventeen_levels=True,
                  active_current=all(.035 < value < .048 for value in active_currents),
                  disabled_current=all(.0005 < value < .004 for value in inactive_currents))
    if interface['filtered']:
        checks.update(weighted_levels=error < .020, shared_bias_ripple=ripple < .015)
    else:
        # Removing a physical bias filter must remain visible in the circuit.
        checks['shared_bias_disturbance_visible'] = ripple > .025
    if load == 0:
        checks['open_load_breaks_nominal_swing'] = max(abs(row[2]) for row in plateau) > 1.5
    result = dict(checks=checks, plateau_V=plateau, ideal_differential_V=target,
                  maximum_plateau_error_V=error, shared_bias_ripple_V=ripple,
                  active_current_range_A=[min(active_currents), max(active_currents)],
                  disabled_current_range_A=[min(inactive_currents), max(inactive_currents)])
    return result


def verify(result):
    failed = [key for key, passed in result['checks'].items() if not passed]
    if failed:
        raise ValueError('physical driver contract failed: ' + ', '.join(failed))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(path): digest(path) for path in
                   (SOURCE, ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path.resolve()) for path in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries, cases={},
                  scope='physical MOS main/history driver with explicit gate charge; generic equations, no process/layout qualification')
    try:
        for name, (load, refined, filtered) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(load), str(int(refined)), str(int(filtered))], stdout=log, stderr=-2,
                        check=True, timeout=600)
            if args.flow == 'veriloga':
                for label, command in [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]:
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=600)
            if args.flow == 'cpp':
                trace = case / 'trace.csv'
                header, rows = read_cpp(trace)
                if header != HEADER:
                    raise ValueError('driver observation interface changed')
            else:
                trace = case / 'spice.dat'
                audit = normalize_roundoff_duplicates(trace)
                rows = read_spice(trace, HEADER)
            interface = json.loads((case / 'interface.json').read_text())
            if (interface['load_ohm'], bool(refined), bool(interface['filtered'])) != (load, refined, filtered):
                raise ValueError('wrong driver experiment parameters')
            result = contract(rows, controls(case / 'controls.csv'), interface)
            compressed = trace.with_suffix(trace.suffix + '.gz')
            with trace.open('rb') as source, gzip.open(compressed, 'wb', compresslevel=6) as destination:
                shutil.copyfileobj(source, destination)
            with gzip.open(compressed, 'rb') as source:
                restored = hashlib.sha256()
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    restored.update(block)
                if restored.hexdigest() != digest(trace):
                    raise ValueError('lossless driver waveform archive verification failed')
            trace.unlink()
            result.update(waveform=str(compressed), waveform_sha256=digest(compressed),
                          controls_sha256=digest(case / 'controls.csv'), interface_sha256=digest(case / 'interface.json'))
            if args.flow == 'veriloga':
                result.update(roundoff_audit_sha256=digest(audit),
                              roundoff_duplicate_count=verify_roundoff_audit(compressed, audit))
            report['cases'][name] = result
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            verify(result)
            print(name + ': pass', flush=True)
        a, b = [report['cases'][name]['plateau_V'] for name in ('nominal', 'refined')]
        delta = max(abs(x - y) for first, second in zip(a, b) for x, y in zip(first, second))
        if delta > .005:
            raise ValueError('driver time-step refinement exceeded 5 mV')
        report['maximum_refinement_difference_V'] = delta
        for path, expected in (hashes | binaries).items():
            if digest(Path(path)) != expected:
                raise ValueError('driver input changed during validation')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    output = args.work / 'comparison.json'
    output.unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        work = args.work.resolve() / flow
        report = json.loads((work / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both independent driver flows must pass first')
        for path, expected in (report['source_sha256'] | report['binary_sha256']).items():
            if digest(Path(path)) != expected:
                raise ValueError('stale driver flow input')
        for name, old in report['cases'].items():
            case = work / name
            for path, expected in [(Path(old['waveform']), old['waveform_sha256']),
                                   (case / 'controls.csv', old['controls_sha256']),
                                   (case / 'interface.json', old['interface_sha256'])]:
                if digest(path) != expected:
                    raise ValueError('driver checked artifact changed')
            if flow == 'veriloga':
                audit = case / 'spice.dat.roundoff.json'
                if (digest(audit) != old['roundoff_audit_sha256']
                        or verify_roundoff_audit(Path(old['waveform']), audit) != old['roundoff_duplicate_count']):
                    raise ValueError('driver raw-time roundoff reconstruction changed')
            current = contract(read_saved(Path(old['waveform']), flow), controls(case / 'controls.csv'),
                               json.loads((case / 'interface.json').read_text()))
            verify(current)
            if any(old[key] != value for key, value in current.items()):
                raise ValueError('driver absolute checks differ on repetition')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('driver flows tested different source')
    differences = {}
    for name in CASES:
        a, b = [report['cases'][name]['plateau_V'] for report in reports]
        maximum = max(abs(x - y) for first, second in zip(a, b) for x, y in zip(first, second))
        if maximum > .010:
            raise ValueError('driver cross-solver plateau difference exceeded 10 mV: ' + name)
        differences[name] = maximum
    output.write_text(json.dumps(dict(status='pass', maximum_plateau_difference_V=differences,
        source_sha256=reports[0]['source_sha256']), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    if args.flow == 'compare':
        compare(args)
    else:
        run(args)
