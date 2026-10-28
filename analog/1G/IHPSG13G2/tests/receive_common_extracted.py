#!/usr/bin/env python3
"""Check both physical-reference flows with the same eight-lane receive contract."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'VerilogA'),
               str(ANALOG / '1G/tests'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from receive_common_extracted import PORTS, reference, separate_host, physical_model, compose
from lead_receive_common_process import process_deck
from lead_receive_common_reference import HEADER, contract, verify
from programmable_receive_gain import windowed_deck
from time_grid import windowed_transient, check_observed_grid
from trace_storage import capture_trace
from trace_states import read_states
from compare import errors
from hardware import source_dependencies
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup, execute


def merge(destination, incoming):
    for path, value in incoming.items():
        if path in destination and destination[path] != value:
            raise ValueError('conflicting receive reference provenance: ' + path)
        destination[path] = value


def layout_evidence(path):
    proof = json.loads(path.read_text())
    if (proof.get('status') != 'pass' or proof.get('local_drc_violations') != 0 or
            proof.get('missing_device_rejected') is not True or
            proof.get('lvs', {}).get('instantiated_devices') != 81 or
            proof.get('connections', {}).get('unattached_endpoints') != 0):
        raise ValueError('passing receive reference geometry and fault rejection required')
    hashes = {}
    for group in ('source_sha256', 'artifact_sha256'):
        merge(hashes, proof[group])
    verify_files(hashes)
    work = Path(proof['work'])
    for name in ('reference.gds', 'extraction/connected.spice', 'export/reference.va', 'export/probes.json'):
        if str(work / name) not in proof['artifact_sha256']:
            raise ValueError('unverified receive reference artifact: ' + name)
    return proof, hashes


def compare_flows(first, second):
    differences = errors(first, second)
    # The external reference and every loaded input retain a 5 mV wiring
    # budget. Internal control/bias nodes have a separate 50 mV budget; both
    # complete trajectories must first pass the same absolute circuit checks.
    limits = [.005 if index in (1, 2, 3) or index >= 8 else .05
              for index in range(1, len(HEADER))]
    checks = {name: value <= limit for name, value, limit in zip(HEADER[1:], differences, limits)}
    return dict(status='pass' if all(checks.values()) else 'fail', checks=checks,
        maximum_differences_V=dict(zip(HEADER[1:], differences)),
        limits_V=dict(zip(HEADER[1:], limits)))


def run(args):
    proof, hashes = layout_evidence(args.layout)
    layout_work = Path(proof['work'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    files = [SOURCE, args.layout.resolve(), args.bench.resolve(), args.ngspice.resolve()]
    files += [SOURCE.with_name(name) for name in ('lead_receive_common_reference.py',
        'receive_common_reference.py', 'receive_common_target.py', 'receive_common_stimulus.py',
        'programmable_receive_gain.py')]
    files += [SOURCE.parents[1] / name for name in ('receive_common_extracted.py',
        'receive_common_layout.py', 'lead_receive_common_process.py', 'reference_clock_extracted_process.py',
        'reference_clock_layout_process.py', 'reduce_extracted_wires.py', 'extract_analog.py',
        'extracted_connections.py', 'resistor_body_extraction.py', 'resistor_body_model.py',
        'spice_vectors.py', 'input_charge.py')]
    files += [ANALOG / 'VerilogA' / name for name in ('compare.py', 'trace_states.py', 'trace_codec.py',
        'trace_storage.py', 'time_grid.py')]
    files += [ANALOG / '1G/tests/hardware.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    files += [p for p in models.rglob('*') if p.is_file() and
              (p.suffix in ('.lib', '.spice', '.osdi') or p.name == '.spiceinit')]
    merge(hashes, {str(p): digest(p) for p in files})
    merge(hashes, source_dependencies([SOURCE.with_name('lead_receive_common_reference.cpp')],
        ANALOG.parent, args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', stage='prepare', work=str(work), source_sha256=hashes,
        corner=args.corner, temperature_C=args.temperature, flows={},
        scope='unfilled shared receive reference with PSP/R3/MIM and distributed internal RC; '
              'eight nominal external input networks and finite supplies; no fill, pad/ESD, gain/ADC or full PHY qualification')
    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')
    try:
        save()
        with (work / 'export.log').open('w') as log:
            execute([args.bench.resolve(), work / 'export', 'export'], stdout=log, stderr=-2,
                    check=True, timeout=60, memory_exclusive=False)
        exported = work / 'export'
        original = (exported / 'bench.cir').read_text()
        mapped, inventory = process_deck((exported / 'design.va').read_text(), original,
                                        models / 'models', args.corner, args.temperature)
        host, mapping, report['boundary'] = separate_host(mapped, inventory,
            layout_work / 'export/reference.va', layout_work / 'export/probes.json')
        host = windowed_deck(host)
        schematic, expected = reference(proof['geometry'])
        physical, probes, report['physical'] = physical_model(layout_work, proof['geometry'],
            {name: name for name in mapping if name not in PORTS}, expected)
        trajectories = []
        for flow, model, bindings in (('schematic', schematic, {n: n for n in mapping}),
                                      ('physical', physical, probes)):
            directory = work / flow
            directory.mkdir()
            deck = compose(host, mapping, model, bindings)
            libraries = ''.join(f'.lib "{models / "models" / name}" {corner}\n'
                                for name, corner in (('cornerRES.lib', 'res_typ'), ('cornerCAP.lib', 'cap_typ')))
            deck = deck.replace('.temp ', libraries + '.temp ', 1)
            deck, grid = windowed_transient(deck, .5e-9 if args.refined else 1e-9)
            path = directory / 'measurement.cir'
            path.write_text(deck)
            merge(hashes, {str(path): digest(path)})
            if args.emit_only:
                report['flows'][flow] = dict(status='netlist_only', deck=str(path))
                continue
            report['stage'] = flow
            save()
            with capture_trace(directory / 'spice.dat'), (directory / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=2147483648', '--', args.ngspice.resolve(), '-b', path],
                    cwd=directory, env=env, stdout=log, stderr=-2, check=True, timeout=21600,
                    memory_exclusive=True)
            log = (directory / 'ngspice.log').read_text()
            if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', log, re.I):
                raise ValueError('receive reference simulation failed: ' + flow)
            trace = directory / 'spice.dat.gz'
            rows = read_states(trace, HEADER)
            result = contract(rows)
            result.update(observed_grid=check_observed_grid(rows, grid), waveform=str(trace),
                          waveform_sha256=digest(trace))
            report['flows'][flow] = result
            save()
            verify(result)
            report['flows'][flow]['status'] = 'pass'
            trajectories.append(rows)
        verify_files(hashes)
        if args.emit_only:
            report.update(status='netlists_emitted', stage='complete')
        else:
            report['comparison'] = compare_flows(*trajectories)
            if report['comparison']['status'] != 'pass':
                raise ValueError('receive reference schematic/extracted comparison failed')
            report.update(status='pass', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'bench', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--refined', action='store_true')
    parser.add_argument('--emit-only', action='store_true')
    run(parser.parse_args())
