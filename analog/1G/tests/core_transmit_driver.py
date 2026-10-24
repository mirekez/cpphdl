#!/usr/bin/env python3
"""Qualify the loaded sixteen-translator bank and weighted line driver."""
import argparse
import bisect
import csv
import gzip
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile

from hardware import configure_cleanup, execute, source_dependencies
from transmit_driver import HEADER as DRIVER_HEADER, controls, contract, digest, verify

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows
from trace_codec import normalize_roundoff_duplicates, verify_roundoff_audit
from trace_storage import capture_trace

HEADER = DRIVER_HEADER + ['TRANSLATION_BIAS', 'VLOGIC', 'LOGIC_DROP'] + [f'CORE{i}' for i in range(16)]
TRIM_HEADER = [name + str(i) for i in range(6) for name in ('TRIM_CORE', 'TRIM_HIGH')]
CASES = {'nominal': (100, False), 'low_load': (80, False), 'high_load': (120, False),
         'refined': (100, True)}


def qualified_controls(fixture):
    evidence = json.loads(fixture.read_text())
    if evidence.get('status') != 'pass':
        raise ValueError('missing qualified controller stimulus')
    if evidence.get('flow') == 'control_cpp' and evidence.get('module') == 'PAM5DriverControl':
        trace = Path(evidence['controls'])
        expected = evidence['controls_sha256']
    elif evidence.get('flow') == 'cpp':
        trace = fixture.parent / 'nominal/controls.csv'
        expected = evidence['cases']['nominal']['controls_sha256']
    else:
        raise ValueError('unknown controller qualification format')
    if not evidence.get('source_sha256') or not evidence.get('binary_sha256') or digest(trace) != expected:
        raise ValueError('missing or changed qualified controller trace')
    for name, expected in (evidence['source_sha256'] | evidence['binary_sha256']).items():
        if digest(Path(name)) != expected:
            raise ValueError('stale controller qualification: ' + name)
    controls(trace)
    return trace


def read_trace(path, flow, header=HEADER):
    with gzip.open(path, 'rt') as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != header:
                raise ValueError('core driver trace interface changed')
        else:
            if len(next(file).split()) != len(header):
                raise ValueError('core driver SPICE interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(header), str(path))


def check_trim_delivery(rows, code):
    if not isinstance(code, int) or not 0 <= code <= 63 or not rows:
        raise ValueError('missing or invalid physical trim profile')
    for row in rows:
        if len(row) != len(HEADER) + len(TRIM_HEADER):
            raise ValueError('missing electrical trim observations')
        for bit in range(6):
            low = bool(code & (1 << bit))
            core, high = row[len(HEADER) + 2 * bit:len(HEADER) + 2 * bit + 2]
            if not all(math.isfinite(v) for v in (core, high)):
                raise ValueError('nonfinite electrical trim voltage')
            core_ok = -.1 < core < .15 if low else 1.05 < core < 1.35
            high_ok = -.1 < high < .3 if low else 3.0 < high < 3.4
            if not core_ok or not high_ok:
                raise ValueError('trim bit did not reach both electrical voltage domains')
    return dict(trim_code=code, every_trim_bit_delivered=True)


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
    if 'trim_code' in interface:
        result.update(check_trim_delivery(steady, interface['trim_code']))
    verify(result)
    return result


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    (root / 'checks.json').unlink(missing_ok=True)
    (root.parent / 'comparison.json').unlink(missing_ok=True)
    # The fixture must come from an independently passing actual controller run.
    fixture = args.fixture.resolve()
    trace = qualified_controls(fixture)
    words = controls(trace)
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('transmit_driver.py'), fixture, trace,
                 ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/trace_codec.py',
                 ANALOG / 'VerilogA/trace_storage.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path.resolve()) for path in (args.openvaf, args.ngspice)})
    header = HEADER + TRIM_HEADER if args.trim_code is not None else HEADER
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  fixture_source=str(args.fixture_source.resolve()), trim_code=args.trim_code,
                  run_directory=str(work),
                  scope='loaded main/history bank with declared core translators and trim; finite supplies and shared bias, no IHP/layout or automatic calibration qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)

    save()
    try:
        for name, (load, refined) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            shutil.copyfile(trace, case / 'controls.csv')
            with (case / 'bench.log').open('w') as log:
                command = [str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                           str(trace), str(load), str(int(refined))]
                if args.trim_code is not None:
                    command.append(str(args.trim_code))
                if args.flow == 'cpp':
                    with capture_trace(case / 'trace.csv') as captured:
                        execute(command, stdout=log, stderr=-2, check=True, timeout=3600)
                else:
                    execute(command, stdout=log, stderr=-2, check=True, timeout=3600)
            if args.flow == 'cpp':
                raw = captured.path
                rows = read_trace(raw, 'cpp', header)
            else:
                with (case / 'compile.log').open('w') as log:
                    execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                            stdout=log, stderr=-2, check=True, timeout=3600)
                with capture_trace(case / 'spice.dat') as captured, (case / 'ngspice.log').open('w') as log:
                    execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                            stdout=log, stderr=-2, check=True, timeout=3600)
                raw = captured.path
                audit = normalize_roundoff_duplicates(raw, epsilon_units=64)
                if json.loads(audit.read_text())['original_sha256'] != captured.metadata['original_sha256']:
                    raise ValueError('roundoff audit does not reconstruct the captured source stream')
                rows = read_trace(raw, 'veriloga', header)
            interface = json.loads((case / 'interface.json').read_text())
            if (interface['load_ohm'] != load or not interface['filtered']
                    or interface.get('trim_code') != args.trim_code):
                raise ValueError('incorrect core driver experiment')
            result = check(rows, words, interface)
            result.update(waveform=str(raw), waveform_sha256=digest(raw),
                          original_stream_sha256=captured.metadata['original_sha256'],
                          controls_sha256=digest(case / 'controls.csv'), interface_sha256=digest(case / 'interface.json'))
            if args.flow == 'veriloga':
                result['roundoff_audit_sha256'] = digest(audit)
            report['cases'][name] = result
            save()
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
        save()


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
            case = Path(report.get('run_directory', root)) / name
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
            header = HEADER + TRIM_HEADER if report.get('trim_code') is not None else HEADER
            current = check(read_trace(wave, flow, header), controls(case / 'controls.csv'),
                            json.loads((case / 'interface.json').read_text()))
            if any(result[key] != value for key, value in current.items()):
                raise ValueError('core driver absolute evidence changed')
        reports.append(report)
    if (reports[0]['source_sha256'] != reports[1]['source_sha256']
            or reports[0].get('trim_code') != reports[1].get('trim_code')
            or reports[0].get('fixture_source') != reports[1].get('fixture_source')):
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
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_suffix('.cpp'))
    parser.add_argument('--trim-code', type=int, choices=range(64))
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.flow != 'compare' and (not args.bench or not args.fixture):
        parser.error('electrical flow requires bench and qualified controller fixture')
    if args.flow == 'veriloga' and (not args.openvaf or not args.ngspice):
        parser.error('Verilog-A flow requires OpenVAF and ngspice')
    compare(args) if args.flow == 'compare' else run(args)
