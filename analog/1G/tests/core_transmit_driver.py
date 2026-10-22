#!/usr/bin/env python3
"""Qualify the loaded sixteen-translator bank and weighted line driver."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

from hardware import configure_cleanup, execute, source_dependencies
from transmit_driver import HEADER as DRIVER_HEADER, controls, contract, digest, verify

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows, read_cpp, read_spice
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit

HEADER = DRIVER_HEADER + ['TRANSLATION_BIAS', 'VLOGIC', 'LOGIC_DROP'] + [f'CORE{i}' for i in range(16)]
CASES = {'nominal': (100, False), 'low_load': (80, False), 'high_load': (120, False),
         'refined': (100, True)}


def read_trace(path, flow):
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != HEADER:
                raise ValueError('core driver trace interface changed')
        else:
            if len(next(file).split()) != len(HEADER):
                raise ValueError('core driver SPICE interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def check(rows, words, interface):
    # Preserve every existing absolute line-driver bound at the delivered gates.
    result = contract(rows, words, interface)
    times = [row[0] for row in rows]

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    for index, word in enumerate(words):
        if index < interface['warm_words']:
            continue
        time = interface['startup_s'] + (index + .8) * interface['ui_s']
        for bank, key in enumerate(('main_p', 'main_n', 'history_p', 'history_n')):
            for bit in range(4):
                value = at(time, 26 + bank * 4 + bit)
                if (value < 1.05 if word[key] & (1 << bit) else value > .15):
                    raise ValueError('core control did not reach its low-voltage rail')
    steady = [row for row in rows if row[0] >= interface['startup_s'] + interface['warm_words'] * interface['ui_s']]
    result['checks'].update(logic_supply=min(row[24] for row in steady) > 1.08,
                            translation_bias_headroom=all(1.2 < row[23] < 2.3 for row in steady))
    result['translation_bias_range_V'] = [min(row[23] for row in steady), max(row[23] for row in steady)]
    result['minimum_logic_supply_V'] = min(row[24] for row in steady)
    verify(result)
    return result


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    # The fixture must come from an independently passing actual controller run.
    fixture = args.fixture.resolve()
    evidence = json.loads(fixture.read_text())
    trace = fixture.parent / 'nominal/controls.csv'
    if (evidence.get('status') != 'pass' or evidence.get('flow') != 'cpp'
            or evidence['cases']['nominal']['controls_sha256'] != digest(trace)):
        raise ValueError('missing or changed qualified controller stimulus')
    for name, value in (evidence['source_sha256'] | evidence['binary_sha256']).items():
        if digest(Path(name)) != value:
            raise ValueError('stale controller qualification: ' + name)
    words = controls(trace)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('transmit_driver.py'), fixture, trace,
                 ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path.resolve()) for path in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='loaded sixteen-translator generic electrical bank; finite supplies and shared bias, no IHP/layout qualification')
    try:
        for name, (load, refined) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            shutil.copyfile(trace, case / 'controls.csv')
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(trace), str(load), str(int(refined))], stdout=log, stderr=-2, check=True, timeout=3600)
            if args.flow == 'cpp':
                raw = case / 'trace.csv'
                header, rows = read_cpp(raw)
                if header != HEADER:
                    raise ValueError('core driver observation interface changed')
            else:
                for label, command in [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]:
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=3600)
                raw = case / 'spice.dat'
                audit = normalize_roundoff_duplicates(raw, epsilon_units=64)
                rows = read_spice(raw, HEADER)
            interface = json.loads((case / 'interface.json').read_text())
            if interface['load_ohm'] != load or not interface['filtered']:
                raise ValueError('incorrect core driver experiment')
            result = check(rows, words, interface)
            archive = raw.with_suffix(raw.suffix + '.gz')
            original = digest(raw)
            with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=6) as output:
                shutil.copyfileobj(source, output)
            with gzip.open(archive, 'rb') as source:
                restored = hashlib.sha256()
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    restored.update(block)
            if restored.hexdigest() != original:
                raise ValueError('core driver archive is not lossless')
            raw.unlink()
            result.update(waveform=str(archive), waveform_sha256=digest(archive),
                          controls_sha256=digest(case / 'controls.csv'), interface_sha256=digest(case / 'interface.json'))
            if args.flow == 'veriloga':
                result['roundoff_audit_sha256'] = digest(audit)
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        for name, value in (hashes | binaries).items():
            if digest(Path(name)) != value:
                raise ValueError('core driver inputs changed during qualification: ' + name)
        nominal, refined = [report['cases'][key]['plateau_V'] for key in ('nominal', 'refined')]
        if len(nominal) != len(refined) or max(abs(a - b) for x, y in zip(nominal, refined) for a, b in zip(x, y)) > .005:
            raise ValueError('core driver refinement exceeds 5 mV')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    work = args.work.resolve()
    (work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        root = work / flow
        report = json.loads((root / 'checks.json').read_text())
        if (report.get('status') != 'pass' or report.get('flow') != flow
                or set(report.get('cases', {})) != set(CASES)):
            raise ValueError('core driver comparison requires independently passing flows')
        for name, value in (report['source_sha256'] | report['binary_sha256']).items():
            if digest(Path(name)) != value:
                raise ValueError('stale core driver evidence: ' + name)
        for name, result in report['cases'].items():
            case = root / name
            wave = Path(result['waveform'])
            if (digest(wave) != result['waveform_sha256']
                    or digest(case / 'controls.csv') != result['controls_sha256']
                    or digest(case / 'interface.json') != result['interface_sha256']):
                raise ValueError('changed core driver waveform or interface')
            if flow == 'veriloga':
                audit = case / 'spice.dat.roundoff.json'
                if digest(audit) != result['roundoff_audit_sha256']:
                    raise ValueError('changed core driver roundoff audit')
                verify_roundoff_audit(wave, audit)
            current = check(read_trace(wave, flow), controls(case / 'controls.csv'),
                            json.loads((case / 'interface.json').read_text()))
            if any(result[key] != value for key, value in current.items()):
                raise ValueError('core driver absolute evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('core driver flows used different sources')
    differences = {}
    for name in CASES:
        a, b = [r['cases'][name]['plateau_V'] for r in reports]
        if len(a) != len(b):
            raise ValueError('core driver plateau counts differ')
        differences[name] = max(abs(x - y) for p, q in zip(a, b) for x, y in zip(p, q))
        if differences[name] > .010:
            raise ValueError('core driver solvers differ by more than 10 mV')
    (work / 'comparison.json').write_text(json.dumps(dict(status='pass', maximum_plateau_difference_V=differences,
        flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.flow != 'compare' and (not args.bench or not args.fixture):
        parser.error('electrical flow requires bench and qualified controller fixture')
    if args.flow == 'veriloga' and (not args.openvaf or not args.ngspice):
        parser.error('Verilog-A flow requires OpenVAF and ngspice')
    compare(args) if args.flow == 'compare' else run(args)
