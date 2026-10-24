#!/usr/bin/env python3
"""Run both generic transmitter flows with every equal-time state retained."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import tempfile

from core_transmit_driver import ANALOG, CASES, HEADER, TRIM_HEADER, qualified_controls
from hardware import configure_cleanup, execute, source_dependencies
from transmit_driver import controls, digest
from transmit_state_contract import check
from trace_states import read_states
from trace_storage import capture_trace, stream_digest

SOURCE = Path(__file__).resolve()


def fresh(hashes):
    if not hashes:
        raise ValueError('missing qualification inputs')
    for name, expected in hashes.items():
        if digest(Path(name)) != expected:
            raise ValueError('changed qualification input: ' + name)


def copy_reusable(args, name, case):
    if args.reuse_report is None:
        return None
    old = json.loads(args.reuse_report.read_text())
    if old.get('flow') != args.flow:
        raise ValueError('reuse flow differs')
    old_inputs = old.get('input_sha256', old.get('binary_sha256', {}))
    if old_inputs.get(str(args.bench.resolve())) != digest(args.bench.resolve()):
        raise ValueError('reused producer differs from current binary')
    if 'input_sha256' in old:
        if old.get('status') != 'pass':
            raise ValueError('all-state reuse receipt did not pass')
        fresh(old['input_sha256'])
    else:
        # A completed saved case can be recovered from an interrupted run.
        # Its electrical checks, artifacts and all original inputs must pass.
        fresh(old['source_sha256'] | old['binary_sha256'])
        if old.get('status') != 'pass' and not (args.flow == 'cpp' and old.get('error') == '[Errno 28] No space left on device'):
            raise ValueError('reuse receipt failed for another reason')
    saved = old.get('cases', {}).get(name)
    if saved is None:
        return None
    if not all(saved.get('checks', {}).values()) or not saved.get('checks'):
        raise ValueError('reused case failed physical requirements')
    if 'artifact_sha256' in saved:
        artifacts = saved['artifact_sha256']
        fresh(artifacts)
        candidates = [Path(path) for path in artifacts if path.endswith('/spice.dat.gz')]
        if len(candidates) != 1:
            raise ValueError('ambiguous reusable waveform')
        wave = candidates[0]
    else:
        wave = Path(saved['waveform'])
        fresh({str(wave): saved['waveform_sha256'],
               str(wave.parent / 'controls.csv'): saved['controls_sha256'],
               str(wave.parent / 'interface.json'): saved['interface_sha256']})
    for filename in ('design.va', 'bench.cir', 'conditions.json', 'columns.csv', 'interface.json', 'controls.csv'):
        if digest(case / filename) != digest(wave.parent / filename):
            raise ValueError('reused experiment differs from current export: ' + filename)
    # Keep canonical traces with their reconstruction audit; read_states restores
    # every original record before applying any electrical measurement.
    target = case / wave.name
    # These verified archives are immutable inputs. A hard link avoids storing
    # a second copy of the complete stream in the same build filesystem.
    if wave.stat().st_dev == case.stat().st_dev:
        os.link(wave, target)
    else:
        shutil.copyfile(wave, target)
    audit = wave.parent / 'spice.dat.roundoff.json'
    if audit.exists():
        shutil.copyfile(audit, case / audit.name)
    return dict(receipt=str(args.reuse_report.resolve()), receipt_sha256=digest(args.reuse_report),
                waveform=str(wave), waveform_sha256=digest(wave))


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='states-', dir=root))
    stimulus = qualified_controls(args.fixture.resolve())
    words = controls(stimulus)
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    files = [SOURCE, SOURCE.with_name('core_transmit_driver.py'), SOURCE.with_name('transmit_driver.py'),
             SOURCE.with_name('transmit_state_contract.py'), args.fixture.resolve(), stimulus,
             ANALOG / 'VerilogA/trace_states.py', ANALOG / 'VerilogA/trace_codec.py',
             ANALOG / 'VerilogA/trace_storage.py', ANALOG / 'VerilogA/compare.py']
    hashes.update({str(path): digest(path) for path in files})
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries |= {str(path.resolve()): digest(path.resolve()) for path in (args.openvaf, args.ngspice)}
    report = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries,
                  cases={}, run_directory=str(work), trim_code=args.trim_code,
                  scope='loaded generic transistor circuit, finite supplies, every solver state; no process/layout/full-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)

    save()
    try:
        for name, (load, refined) in CASES.items():
            case = work / name
            case.mkdir()
            shutil.copyfile(stimulus, case / 'controls.csv')
            command = [str(args.bench.resolve()), str(case), 'export', str(stimulus), str(load), str(int(refined))]
            if args.trim_code is not None:
                command.append(str(args.trim_code))
            with (case / 'export.log').open('w') as log:
                execute(command, stdout=log, stderr=-2, check=True, timeout=120)
            reused = copy_reusable(args, name, case)
            if reused is None:
                if args.flow == 'cpp':
                    command[2] = 'run'
                    with capture_trace(case / 'trace.csv'), (case / 'bench.log').open('w') as log:
                        execute(command, stdout=log, stderr=-2, check=True, timeout=3600)
                else:
                    with (case / 'compile.log').open('w') as log:
                        execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                                stdout=log, stderr=-2, check=True, timeout=120)
                    with capture_trace(case / 'spice.dat'), (case / 'ngspice.log').open('w') as log:
                        execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=case,
                                stdout=log, stderr=-2, check=True, timeout=3600)
            wave = case / ('trace.csv.gz' if args.flow == 'cpp' else 'spice.dat.gz')
            audit = case / 'spice.dat.roundoff.json'
            interface = json.loads((case / 'interface.json').read_text())
            if interface['load_ohm'] != load or not interface['filtered'] or interface.get('trim_code') != args.trim_code:
                raise ValueError('incorrect transmitter experiment')
            header = HEADER + TRIM_HEADER if args.trim_code is not None else HEADER
            rows = read_states(wave, header, csv_format=args.flow == 'cpp', audit=audit if audit.exists() else None)
            result = check(rows, words, interface)
            result['retained_rows'] = len(rows)
            result['equal_time_records'] = sum(a[0] == b[0] for a, b in zip(rows, rows[1:]))
            del rows
            with gzip.open(wave, 'rb') as stream:
                result['stored_stream_sha256'] = stream_digest(stream)
            artifacts = [wave, case / 'controls.csv', case / 'interface.json', case / 'design.va',
                         case / 'bench.cir', case / 'conditions.json', case / 'columns.csv']
            if audit.exists():
                artifacts.append(audit)
            result.update(artifact_sha256={str(path): digest(path) for path in artifacts}, reused_simulation=reused)
            report['cases'][name] = result
            save()
            print(name, result['maximum_all_state_plateau_error_V'], flush=True)
        a, b = [report['cases'][name]['plateau_bounds_V'] for name in ('nominal', 'refined')]
        difference = max(abs(x - y) for p, q in zip(a, b) for r, s in zip(p, q) for x, y in zip(r, s))
        if len(a) != len(b) or difference > .005:
            raise ValueError('transmitter timestep refinement exceeds 5 mV')
        fresh(hashes | binaries)
        report.update(status='pass', maximum_refinement_difference_V=difference)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


def compare(args):
    reports = [json.loads((args.work / flow / 'checks.json').read_text()) for flow in ('cpp', 'veriloga')]
    for flow, report in zip(('cpp', 'veriloga'), reports):
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both complete flows must pass independently')
        fresh(report['source_sha256'] | report['binary_sha256'])
        for result in report['cases'].values():
            fresh(result['artifact_sha256'])
            paths = [Path(path) for path in result['artifact_sha256']]
            wave = next(path for path in paths if path.name in ('trace.csv.gz', 'spice.dat.gz'))
            case = wave.parent
            audit = case / 'spice.dat.roundoff.json'
            header = HEADER + TRIM_HEADER if report['trim_code'] is not None else HEADER
            rows = read_states(wave, header, csv_format=flow == 'cpp', audit=audit if audit.exists() else None)
            current = check(rows, controls(case / 'controls.csv'), json.loads((case / 'interface.json').read_text()))
            if any(result.get(key) != json.loads(json.dumps(value)) for key, value in current.items()):
                raise ValueError('transmitter electrical receipt differs from current checking')
            del rows
    if reports[0]['source_sha256'] != reports[1]['source_sha256'] or reports[0]['trim_code'] != reports[1]['trim_code']:
        raise ValueError('transmitter flows used different sources or trim')
    differences = {}
    for name in CASES:
        a, b = [r['cases'][name]['plateau_bounds_V'] for r in reports]
        if len(a) != len(b):
            raise ValueError('transmitter aperture counts differ')
        differences[name] = max(abs(x - y) for p, q in zip(a, b) for r, s in zip(p, q) for x, y in zip(r, s))
        if differences[name] > .010:
            raise ValueError('transmitter cross-simulator difference exceeds 10 mV')
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', maximum_difference_V=differences,
        input_receipts={str(args.work / flow / 'checks.json'): digest(args.work / flow / 'checks.json')
                        for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--fixture-source', type=Path)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--trim-code', type=int, choices=range(64))
    parser.add_argument('--reuse-report', type=Path)
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
