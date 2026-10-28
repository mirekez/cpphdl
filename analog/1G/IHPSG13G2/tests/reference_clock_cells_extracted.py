#!/usr/bin/env python3
"""Check routed PFD behavior independently, then compare against passing PSP cells."""
import argparse
import json
import math
import os
from pathlib import Path
import runpy
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'VerilogA'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from reference_clock_cells import detector
from reference_clock_divider_cells import divider
from trace_storage import archive_trace
from tool_process import configure_cleanup, execute

def comparison(result, reference, module):
    a, b = result['plateau_probes_V'], reference['plateau_probes_V']
    if len(a) != len(b) or any(len(x) != len(y) for x, y in zip(a, b)):
        raise ValueError('clock comparison sample counts differ')
    delta = [x - y for first, second in zip(a, b) for x, y in zip(first, second)]
    rms = math.sqrt(sum(v * v for v in delta) / len(delta))
    if module == 'detector':
        error = abs(result['mean_signed_error_V'] - reference['mean_signed_error_V'])
        checks = dict(plateau_rms_difference_V=rms, mean_signed_error_difference_V=error)
        limit = .01
    else:
        a, b = result['edges'], reference['edges']
        if len(a) != len(b) or any(x[1] != y[1] for x, y in zip(a, b)):
            raise ValueError('divider edge sequence differs')
        error = max(abs(x[0] - y[0]) for x, y in zip(a, b))
        checks = dict(plateau_rms_difference_V=rms, maximum_edge_difference_s=error)
        limit = .1e-9
    return checks, rms <= .02 and error <= limit


def run(args):
    fixture_path = SOURCE.with_name('reference_clock_cells.py' if args.module == 'detector'
                                    else 'reference_clock_divider_cells.py')
    fixture = runpy.run_path(str(fixture_path))
    digest, bench, contract, read = (fixture[name] for name in ('digest', 'bench', 'contract', 'read'))
    circuit = detector() if args.module == 'detector' else divider()
    circuit_name = circuit.splitlines()[0].split()[1]
    cases = ([(profile + ('_loaded' if load else '_unloaded'), profile, load)
              for load in (0, 20e-15)
              for profile in ('same', 'ref_leads', 'fb_leads', 'fb_wrapped', 'slow', 'fast', 'missing')]
             if args.module == 'detector' else [('unloaded', 'divider', False), ('loaded', 'divider', True)])
    geometry = json.loads(args.layout.read_text())
    schematic = json.loads(args.schematic.read_text())
    if (geometry.get('status') != 'pass' or geometry.get('local_drc_violations') != 0 or
            geometry.get('module', 'detector') != args.module or not geometry.get('cut_reference_rejected') or schematic.get('status') != 'pass' or
            schematic.get('corner') != args.corner or set(schematic.get('cases', {})) != {name for name, _, _ in cases}):
        raise ValueError('fresh independent geometry and complete schematic corner proofs required')
    hashes = {}

    def merge(values):
        for path, value in values.items():
            if path in hashes and hashes[path] != value:
                raise ValueError('detector proofs used different input revisions: ' + path)
            hashes[path] = value

    for evidence in (geometry, schematic):
        for group in ('source_sha256', 'artifact_sha256'):
            merge(evidence.get(group, {}))
    merge(geometry['extraction']['artifact_sha256'])
    for case in schematic['cases'].values():
        merge(case['artifact_sha256'])
    paths = [SOURCE, args.layout.resolve(), args.schematic.resolve(),
             fixture_path, ANALOG / 'VerilogA/trace_storage.py']
    merge({str(p): digest(p) for p in paths})
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('stale detector source, geometry or schematic evidence')
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    if any(str(p) not in hashes for p in (args.ngspice.resolve(), models / '.spiceinit', models / 'models/cornerMOSlv.lib')):
        raise ValueError('physical simulation must use the qualified schematic tool and PDK')
    physical_path = Path(geometry['work']) / 'extraction/conserved.spice'
    if str(physical_path) not in hashes:
        raise ValueError('physical detector circuit identity missing')
    physical = physical_path.read_text()
    declaration = '.subckt ExtractedAnalog ' + ' '.join(geometry['geometry']['ports'])
    if physical.count(declaration) != 1:
        raise ValueError('physical detector port interface changed')
    physical = physical.replace(declaration, declaration.replace('ExtractedAnalog', circuit_name), 1)
    temperature = dict(mos_tt=27, mos_ss=125, mos_ff=-40)[args.corner]
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', module=args.module, work=str(work), source_sha256=hashes, corner=args.corner,
        temperature_C=temperature, cases={},
        scope='routed clock component alone: shared absolute checks plus PSP-schematic comparison; excludes chip fill, field calibration and complete PLL')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        for name, profile, load in cases:
            case = work / name
            case.mkdir()
            text = (bench('', models, args.corner, temperature, profile, load)
                    if args.module == 'detector' else bench('', models, args.corner, temperature, load))
            if text.count(circuit) != 1:
                raise ValueError('changed shared clock experiment')
            deck = case / 'bench.cir'
            deck.write_text(text.replace(circuit, physical, 1))
            with (case / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'bench.cir'],
                    cwd=case, env=dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                        SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1'),
                    stdout=log, stderr=-2, check=True, timeout=900, memory_exclusive=False)
            rows = read(case / 'spice.dat' if args.module == 'detector' else case, 'veriloga')
            result = contract(rows, profile)
            result['comparison'], matches = comparison(result, schematic['cases'][name], args.module)
            report['cases'][name] = result
            if not matches:
                raise ValueError(name + ': independently passing physical and schematic clock circuits differ')
            archive, result['trace_storage'] = archive_trace(case / 'spice.dat')
            result['artifact_sha256'] = {str(p): digest(p) for p in (archive, deck)}
            save()
            print(name + ': pass', flush=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('physical PFD inputs changed')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', choices=('detector', 'divider'), default='detector')
    for name in ('layout', 'schematic', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), required=True)
    run(parser.parse_args())
