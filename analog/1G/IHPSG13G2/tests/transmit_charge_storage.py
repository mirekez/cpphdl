#!/usr/bin/env python3
"""Qualify retained storage geometry for the candidate PSP current-cell realization."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / '1G/tests'),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_charge_realization import charge_plan, create_charge_storage
from named_node_extraction import extract_named
from extracted_connections import check_capacitor_connections
from extract_analog import digest
from input_charge import verify_files
from hardware import source_dependencies
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    proof = json.loads(args.current_proof.read_text())
    if proof.get('status') != 'pass':
        raise ValueError('passing current-cell geometry required')
    hashes = {}

    def merge(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ('source_sha256', 'artifact_sha256'):
                    verify_files(child)
                    for path, sha in child.items():
                        if path in hashes and hashes[path] != sha:
                            raise ValueError('conflicting current-cell evidence')
                        hashes[path] = sha
                else:
                    merge(child)
        elif isinstance(value, list):
            for child in value:
                merge(child)
    merge(proof)
    geometry = {name: proof['cases'][name]['geometry'] for name in ('main', 'history')}
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    env = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'],
               PDK_ROOT=str(pdk), PDK='ihp-sg13g2', OMP_NUM_THREADS='1')
    paths = [SOURCE, args.current_proof.resolve(), args.exporter.resolve(), tools / 'root/usr/bin/klayout']
    for module in list(sys.modules.values()):
        filename = getattr(module, '__file__', None)
        if filename:
            path = Path(filename).resolve()
            if path.is_relative_to(ROOT) and 'build' not in path.relative_to(ROOT).parts:
                paths.append(path)
    pcell = tech.parent / 'python/sg13g2_pycell_lib'
    paths += [p for directory in (tech, pcell) for p in directory.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.drc', '.lvs')]
    for path in paths:
        sha = digest(path)
        if str(path) in hashes and hashes[str(path)] != sha:
            raise ValueError('stale storage input: ' + str(path))
        hashes[str(path)] = sha
    hashes.update(source_dependencies([SOURCE.with_name('transmit_physical_export.cpp')],
        ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='candidate PSP charge realization: two retained capacitors per current cell; '
              'local DRC/LVS, missing-capacitor/tap rejection and conserved RC; '
              'loaded electrical response, fill and full-chip qualification remain separate')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(directory, label, argv):
        report['stage'] = directory.name + ':' + label
        save()
        with (directory / (label + '.log')).open('w') as log:
            result = execute(['prlimit', '--as=1073741824', '--', *map(str, argv)], cwd=directory,
                env=env, stdout=log, stderr=-2, timeout=900, memory_exclusive=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(label + ' execution failed')

    try:
        save()
        command(work, 'export', [args.exporter.resolve(), work / 'export'])
        report['charge_ownership'] = charge_plan(work / 'export', geometry)
        hashes.update({str(p): digest(p) for p in (work / 'export').iterdir() if p.is_file()})
        for bank in ('main', 'history'):
            directory = work / bank
            directory.mkdir()
            layout, top, info = create_charge_storage(pdk, work / 'export', geometry, bank == 'history')
            ports = list(info['terminals'])
            gds, schematic = directory / 'storage.gds', directory / 'storage.cir'
            layout.write(str(gds))
            schematic.write_text(info['schematic'])
            result = report['cases'][bank] = dict(geometry=info)

            def lvs(path, destination):
                command(directory, destination.name, [sys.executable, tech / 'lvs/run_lvs.py',
                    '--layout', path, '--netlist', schematic, '--topcell', top.name,
                    '--run_mode', 'deep', '--run_dir', destination])
                return check_lvs(destination / (path.stem + '.lvsdb'), top.name, set(ports), 2)

            result['lvs'] = lvs(gds, directory / 'lvs')
            command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                '--topcell', top.name, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
            drc = directory / 'drc' / ('storage_' + top.name + '_full.lyrdb')
            result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
            violations = Counter(item.findtext('category').strip("'")
                                 for item in ET.parse(drc).getroot().find('items'))
            minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
            if set(violations) - minima:
                raise ValueError(bank + ': local geometry or maximum-density violations')
            result.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
            artifacts = [gds, schematic, drc, directory / 'lvs/storage.lvsdb']
            for fault, cell_name in (('missing_capacitor', info['components'][0]['name']),
                                     ('missing_substrate_tap', info['substrate_tap']['cell'])):
                victim, = [inst for inst in top.each_inst() if inst.cell.name == cell_name]
                index, transform = victim.cell_index, victim.trans
                victim.delete()
                broken = directory / (fault + '.gds')
                layout.write(str(broken))
                try:
                    lvs(broken, directory / (fault + '-lvs'))
                except ValueError:
                    result[fault + '_rejected'] = True
                else:
                    raise ValueError('LVS accepted ' + fault)
                top.insert(db.CellInstArray(index, transform))
                artifacts.extend([broken, directory / (fault + '-lvs') / (fault + '.lvsdb')])
            extraction = extract_named(gds, top.name, ports, pdk, args.magic, directory / 'extraction', substrate='VSS')
            if extraction['inventory']['models'] != {'cap_cmim': 2}:
                raise ValueError('extraction changed the retained capacitor inventory')
            result['extraction'] = extraction
            physical = directory / 'extraction/conserved.spice'
            result['connections'] = check_capacitor_connections(physical.read_text(), ports)
            merge(extraction)
            artifacts.append(physical)
            result['artifact_sha256'] = {str(p): digest(p) for p in artifacts}
            result['status'] = 'pass'
            save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS candidate retained current-cell storage geometry, faults and extraction')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'exporter', 'current-proof', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
