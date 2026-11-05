#!/usr/bin/env python3
"""Check the routed mixed clock subsystem with full PDK DRC/LVS and open-wire control."""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
from checks import check_drc
from integrate_clock_control import ROOT, TOP
from package_development_layout import digest
from tool_process import configure_cleanup, execute


def run(args):
    work, implementation = args.work.resolve(), args.implementation.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if (work / 'checks.json').exists():
        raise ValueError('use a fresh qualification directory')
    generated = json.loads((implementation / 'checks.json').read_text())
    if generated.get('status') != 'routed_candidate' or generated.get('router_drc_violations') != 0:
        raise ValueError('completed detailed routing with zero router violations required')
    hashes = dict(generated['source_sha256'])
    for name in ('system.gds', 'checks.json'):
        path = implementation / name
        hashes[str(path)] = digest(path)
    if digest(implementation / 'system.gds') != generated['artifact_sha256']['system.gds']:
        raise ValueError('routed geometry differs from its receipt')
    tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech'
    for path in [Path(__file__), HERE / 'fill_core.py', HERE / 'checks.py',
                 Path(__file__).with_name('clock_control_connections.py'),
                 Path(__file__).with_name('clock_control_lvs.py'),
                 args.tools.resolve() / 'root/usr/bin/klayout']:
        hashes[str(path)] = digest(path)
    for folder in ('drc', 'lvs', 'macros'):
        for path in (tech / folder).rglob('*'):
            if path.is_file() and path.suffix in ('.drc', '.lvs', '.lym', '.py', '.json'):
                hashes[str(path)] = digest(path)
    def verify():
        for name, expected in hashes.items():
            if digest(Path(name)) != expected:
                raise ValueError('qualification input changed: ' + name)
    report = dict(status='running', top=TOP, full_chip_qualified=False,
                  electrical_qualified=False, source_sha256=hashes, stages=[])
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    env = dict(os.environ, REPEATER_PDK=str(args.pdk.resolve()), REPEATER_TOP=TOP,
               PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
    def command(name, argv, timeout=600):
        report['stage'] = name
        save()
        with (work / (name + '.log')).open('w') as log:
            result = execute([str(x) for x in argv], env=env, stdout=log, stderr=-2,
                             timeout=timeout, memory_exclusive=name in ('fill', 'drc'))
        report['stages'].append(dict(name=name, returncode=result.returncode))
        return result.returncode
    try:
        verify()
        filled = work / 'system_filled.gds'
        if command('fill', [args.tools.resolve() / 'bin/klayout', '-zz', '-r', HERE / 'fill_core.py',
                           '-rd', 'output_file=' + str(filled), implementation / 'system.gds']):
            raise ValueError('fill failed')
        failures = []
        code = command('connections', [sys.executable, Path(__file__).with_name('clock_control_connections.py'),
                       '--gds', filled, '--work', work / 'connections'])
        report['connections'] = json.loads((work / 'connections/checks.json').read_text())
        if code:
            failures.append('metal connectivity/open-wire control failed')
        code = command('transistor_lvs', [sys.executable, Path(__file__).with_name('clock_control_lvs.py'),
                       '--gds', filled, '--work', work / 'transistor_lvs',
                       '--pdk', args.pdk, '--tools', args.tools,
                       '--timeout', args.lvs_timeout], timeout=args.lvs_timeout + 60)
        report['transistor_lvs'] = json.loads((work / 'transistor_lvs/checks.json').read_text())
        if code:
            failures.append('transistor LVS failed')
        code = command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', filled,
                       '--topcell', TOP, '--run_dir', work / 'drc', '--mp', '1',
                       '--density_thr', '1', '--antenna'], timeout=900)
        try:
            report['drc'] = check_drc(work / 'drc/system_filled_ProcessingClockSystem_full.lyrdb', tech / 'drc')
            if code:
                raise ValueError('native DRC process failed')
        except ValueError as error:
            failures.append(str(error))
        verify()
        if failures:
            raise ValueError('; '.join(failures))
        report.update(status='physical_checks_pass', stage='complete', gds=str(filled),
                      drc_qualified=True, lvs_qualified=True, opened_routing_rejected=True)
        print('PASS clock/control: full DRC, 20,938-device LVS and opened-routing control')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p):digest(p) for p in work.rglob('*')
            if p.is_file() and p.name != 'checks.json'}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--implementation', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--lvs-timeout', type=int, default=900)
    run(parser.parse_args())
