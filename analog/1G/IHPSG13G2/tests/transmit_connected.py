#!/usr/bin/env python3
"""Check the complete loaded TX with independent schematic and component RC."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / '1G/tests'), str(SOURCE.parent),
               str(ROOT / 'VerilogA'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_physical_models import load_models
from transmit_connected_deck import experiment, compare
from core_transmit_driver import qualified_controls
from transmit_driver import controls
from transmit_state_contract import check
from extract_analog import digest
from hardware import source_dependencies
from trace_states import read_states
from reserved_trace import capture_reserved_trace
from tool_process import execute, configure_cleanup, memory_slot


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', stage='prepare', work=str(work), flows={},
        corner=args.corner, temperature_C=args.temperature, trim_code=args.trim_code,
        scope='loaded complete C++ transmitter partition with physical MOS/MIM storage; '
              'each component retains extracted RC, inter-macro wires remain ideal; '
              'MOSCAP TT, other MOS at declared corner, nominal passives and finite supplies; '
              'excludes routed TX, automatic calibration, extracted PVT storage and full-chip qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def fresh():
        for path, value in report['source_sha256'].items():
            if not Path(path).is_file() or digest(path) != value:
                raise ValueError('connected transmitter inputs changed: ' + path)

    try:
        save()
        stimulus = qualified_controls(args.fixture.resolve())
        words = controls(stimulus)
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work / 'export'], stdout=log, stderr=-2,
                    check=True, timeout=120, memory_exclusive=False)
        circuits, evidence = load_models(work / 'export', args.proofs.resolve())
        report['components'] = evidence
        hashes = dict(evidence['source_sha256'])
        hashes.update(source_dependencies([SOURCE.with_name('transmit_physical_export.cpp')],
            ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]))
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths = [SOURCE, args.exporter.resolve(), args.ngspice.resolve(),
                 args.fixture.resolve(), stimulus, models / '.spiceinit']
        paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
        paths += [p for p in (work / 'export').iterdir() if p.is_file()]
        # Include imported project helpers as well as the recursively verified
        # geometry/model receipts; changing a checker invalidates this run.
        for module in list(sys.modules.values()):
            path = getattr(module, '__file__', None)
            if path and Path(path).resolve().is_relative_to(ROOT):
                candidate = Path(path).resolve()
                if 'build' not in candidate.relative_to(ROOT).parts and 'pdk/IHPSG13G2' not in str(candidate):
                    paths.append(candidate)
        for path in paths:
            path = Path(path).resolve()
            value = digest(path)
            if str(path) in hashes and hashes[str(path)] != value:
                raise ValueError('inconsistent connected transmitter evidence: ' + str(path))
            hashes[str(path)] = value
        report['source_sha256'] = hashes
        shutil.copyfile(stimulus, work / 'controls.csv')
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
            SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1', OMP_THREAD_LIMIT='1')
        prepared = {}
        # Emit both experiments before queueing heavy work so both are reviewable.
        for flow in ('schematic', 'physical'):
            directory = work / flow
            directory.mkdir()
            deck, interface, header = experiment(circuits[flow], evidence['assembly'][flow], words,
                models / 'models', corner=args.corner, temperature=args.temperature, trim=args.trim_code)
            path = directory / 'bench.cir'
            path.write_text(deck)
            (directory / 'interface.json').write_text(json.dumps(interface, indent=2) + '\n')
            (directory / 'columns.json').write_text(json.dumps(header) + '\n')
            for artifact in directory.iterdir():
                hashes[str(artifact)] = digest(artifact)
            prepared[flow] = directory, interface, header
        fresh()
        save()
        if args.emit_only:
            report.update(status='netlists_emitted', stage='complete')
            return
        for flow, (directory, interface, header) in prepared.items():
            report['stage'] = 'waiting_' + flow
            save()
            # Retain admission through export and checking: wrdata and the
            # waveform reader can allocate more memory than the transient.
            with memory_slot(['connected-transmitter'], exclusive=True):
                fresh()
                report['stage'] = flow
                save()
                with capture_reserved_trace(directory / 'trace.dat', 512 * 1024 ** 2) as captured:
                    with (directory / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=3221225472', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=directory, env=environment, stdout=log, stderr=-2, check=True,
                            timeout=21600, memory_exclusive=False)
                report.setdefault('traces', {})[flow] = captured.metadata
                save()
                log = (directory / 'ngspice.log').read_text()
                if ('ngspice-47 done' not in log or
                        re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small', log, re.I)):
                    raise ValueError(flow + ': loaded transmitter simulation failed or incomplete')
                rows = read_states(captured.path, header)
                if (rows[0][0] < 0 or rows[0][0] > 1e-12 or
                        re.findall(r'No[.] of Data Rows\s*:\s*(\d+)', log) != [str(len(rows))]):
                    raise ValueError(flow + ': missing startup or incomplete solver stream')
                maximum_exposure = [max(row[-22 + index] for row in rows) for index in range(22)]
                if any(min(row[-22 + index] for row in rows) < 0 or maximum_exposure[index] > 1.65
                       for index in range(22)):
                    raise ValueError(flow + ': actual translator LV gate exposure exceeds 1.65 V')
                for row in rows:
                    del row[-22:]
                cascode = [row.pop() for row in rows]
                bias = [row[header.index('BIAS')] for row in rows]
                result = dict(cascode_storage_voltage_range_V=[min(cascode), max(cascode)],
                    maximum_translator_lv_gate_exposure_V=maximum_exposure,
                    moscap_voltage_range_V=[min(bias), max(bias)], retained_rows=len(rows),
                    equal_time_records=sum(a[0] == b[0] for a, b in zip(rows, rows[1:])),
                    trace_storage=captured.metadata,
                    artifact_sha256={str(p): digest(p) for p in (captured.path, directory / 'ngspice.log')})
                report['flows'][flow] = result
                if not -.1 <= min(cascode) <= max(cascode) <= 2.4 or not -.1 <= min(bias) <= max(bias) <= 1.3:
                    raise ValueError(flow + ': storage voltage exceeded characterized range')
                result.update(check(rows, words, interface))
                result['status'] = 'pass'
                del rows, cascode, bias
                fresh()
                save()
                print(flow, 'PASS', result['maximum_all_state_plateau_error_V'], flush=True)
        report['comparison'] = compare(report['flows']['schematic'], report['flows']['physical'])
        fresh()
        report.update(status='pass', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exporter', 'fixture', 'proofs', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--trim-code', type=int, choices=range(64), default=44)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--emit-only', action='store_true')
    run(parser.parse_args())
