#!/usr/bin/env python3
"""Validate retained Verilog-A runs after a repeated-time reader failure.

Keep the failed receipt. Require unchanged sources/tools, regenerated identical
circuits and rebuilt identical OSDI objects. No transient simulation is rerun.
"""
import argparse
import json
from pathlib import Path
import tempfile

from core_transmit_driver import ANALOG, CASES, HEADER, TRIM_HEADER, qualified_controls
from hardware import execute
from transmit_driver import controls, digest
from transmit_state_contract import check
from trace_states import read_states


def recheck(args):
    args.output.unlink(missing_ok=True)
    original = json.loads(args.report.read_text())
    if (original.get('flow') != 'veriloga' or original.get('status') != 'fail'
            or original.get('error') != 'backward time or changed electrical state at repeated SPICE time'
            or set(original.get('cases', {})) != set(CASES) - {'refined'}):
        raise ValueError('expected completed runs interrupted only by the refined trace reader')
    hashes = original['source_sha256'] | original['binary_sha256']
    for tool in (args.bench, args.openvaf):
        if str(tool.resolve()) not in original['binary_sha256']:
            raise ValueError('unqualified retained producer/compiler')
    checker_paths = [Path(__file__).resolve(), Path(__file__).with_name('transmit_state_contract.py').resolve(),
                     ANALOG / 'VerilogA/trace_states.py']
    checker_hashes = {str(path): digest(path) for path in checker_paths}
    hashes |= checker_hashes
    hashes[str(args.report.resolve())] = digest(args.report)

    def fresh():
        for name, expected in hashes.items():
            if digest(Path(name)) != expected:
                raise ValueError('changed retained simulation input: ' + name)

    fresh()
    root = Path(original['run_directory'])
    fixtures = [Path(name) for name in original['source_sha256'] if name.endswith('checks.json')]
    if len(fixtures) != 1:
        raise ValueError('ambiguous controller fixture')
    stimulus = qualified_controls(fixtures[0])
    words = controls(stimulus)
    header = HEADER + TRIM_HEADER if original['trim_code'] is not None else HEADER
    report = dict(status='running', flow='veriloga', cases={}, input_sha256=hashes,
                  scope='current all-state validation of retained solver data; no new transient simulation; generic models only')
    try:
        for name, (load, refined) in CASES.items():
            case = root / name
            wave, audit = case / 'spice.dat.gz', case / 'spice.dat.roundoff.json'
            artifacts = [case / item for item in ('spice.dat.gz', 'interface.json', 'controls.csv', 'design.va',
                         'design.osdi', 'bench.cir', 'conditions.json', 'columns.csv', 'ngspice.log')]
            if audit.exists():
                artifacts.append(audit)
            before = {str(path): digest(path) for path in artifacts}
            log = (case / 'ngspice.log').read_text()
            if 'ngspice-47 done' not in log or 'No. of Data Rows :' not in log:
                raise ValueError('retained solver did not finish')
            old = original['cases'].get(name)
            if old:
                for path, key in [(wave, 'waveform_sha256'), (case / 'interface.json', 'interface_sha256'),
                                  (case / 'controls.csv', 'controls_sha256'), (audit, 'roundoff_audit_sha256')]:
                    if digest(path) != old[key]:
                        raise ValueError('changed retained artifact: ' + str(path))
            if digest(case / 'controls.csv') != digest(stimulus):
                raise ValueError('retained stimulus differs from qualified controller')
            with tempfile.TemporaryDirectory(prefix='reexport-', dir=args.output.parent) as temporary:
                destination = Path(temporary)
                command = [str(args.bench.resolve()), str(destination), 'export', str(stimulus),
                           str(load), str(int(refined))]
                if original['trim_code'] is not None:
                    command.append(str(original['trim_code']))
                with (destination / 'export.log').open('w') as output:
                    execute(command, stdout=output, stderr=-2, check=True, timeout=120)
                    for filename in ('interface.json', 'columns.csv', 'conditions.json', 'design.va', 'bench.cir'):
                        if digest(destination / filename) != digest(case / filename):
                            raise ValueError('re-export differs from retained experiment: ' + filename)
                    execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=destination,
                            stdout=output, stderr=-2, check=True, timeout=120)
                    if digest(destination / 'design.osdi') != digest(case / 'design.osdi'):
                        raise ValueError('rebuilt model differs from retained simulation object')
            interface = json.loads((case / 'interface.json').read_text())
            if interface['load_ohm'] != load or not interface['filtered'] or interface.get('trim_code') != original['trim_code']:
                raise ValueError('retained physical profile changed')
            rows = read_states(wave, header, audit=audit if audit.exists() else None)
            result = check(rows, words, interface)
            result.update(retained_rows=len(rows), equal_time_records=sum(a[0] == b[0] for a, b in zip(rows, rows[1:])),
                          artifact_sha256=before)
            for path, expected in before.items():
                if digest(Path(path)) != expected:
                    raise ValueError('retained artifact changed during validation')
            report['cases'][name] = result
            print(name, result['maximum_all_state_plateau_error_V'], flush=True)
            del rows
        nominal, refined = [report['cases'][name]['plateau_bounds_V'] for name in ('nominal', 'refined')]
        difference = max(abs(a - b) for x, y in zip(nominal, refined)
                         for p, q in zip(x, y) for a, b in zip(p, q))
        if len(nominal) != len(refined) or difference > .005:
            raise ValueError('retained timestep refinement exceeds 5 mV')
        report['maximum_refinement_difference_V'] = difference
        fresh()
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--openvaf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    recheck(parser.parse_args())
