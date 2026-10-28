#!/usr/bin/env python3
"""Qualify retained TX storage topology, local geometry and extracted connectivity."""
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
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_storage_layout import create_current_storage, create_output_storage, storage_partition
from named_node_extraction import extract_named
from extracted_connections import check_capacitor_connections
from extract_analog import digest
from hardware import source_dependencies
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    paths = [SOURCE, args.exporter.resolve(), tools / 'root/usr/bin/klayout']
    paths += [SOURCE.parents[1] / name for name in ('transmit_storage_layout.py', 'transmit_storage_substrate.py',
        'transmit_physical_partition.py', 'transmit_current_binding.py', 'core_driver_process.py',
        'cascoded_driver_process.py', 'reference_clock_row.py', 'processing_clock_gate_layout.py',
        'capacitor_layout.py', 'capacitor_bank_layout.py', 'mos_layout.py',
        'named_node_extraction.py', 'extracted_connections.py', 'extract_analog.py')]
    paths += [ANALOG / '1G/tests/hardware.py']
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    pcell = tech.parent / 'python/sg13g2_pycell_lib'
    paths += [p for parent in (tech, pcell) for p in parent.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.drc', '.lvs')]
    hashes = {str(p): digest(p) for p in paths}
    hashes.update(source_dependencies([SOURCE.with_name('transmit_physical_export.cpp')],
        ANALOG.parent, args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='80 local and two output C++ capacitors, local DRC/LVS, missing-capacitor/tap rejection and conserved RC; '
              '400 pF/50 pF bias intentions use separately qualified MOS storage; loaded transmitter, fill and chip checks separate')
    env = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'],
               PDK_ROOT=str(pdk), PDK='ihp-sg13g2', OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(directory, label, argv):
        report['stage'] = directory.name + ':' + label
        save()
        with (directory / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=directory, env=env,
                stdout=log, stderr=-2, timeout=900, memory_exclusive=False).returncode

    try:
        save()
        if command(work, 'export', [args.exporter.resolve(), work / 'export']):
            raise ValueError('transmitter export failed')
        hashes.update({str(p): digest(p) for p in (work / 'export').iterdir() if p.is_file()})
        report['partition'] = storage_partition(work / 'export')
        for case in ('main', 'history', 'output'):
            directory = work / case
            directory.mkdir()
            if case == 'output':
                layout, top, geometry = create_output_storage(pdk, work / 'export')
            else:
                layout, top, geometry = create_current_storage(pdk, work / 'export', case == 'history')
            ports = list(geometry['terminals'])
            result = dict(geometry=geometry)
            report['cases'][case] = result
            count = sum(c['geometry'].get('capacitors', 1) for c in geometry['components'])
            gds, schematic = directory / 'storage.gds', directory / 'storage.cir'
            layout.write(str(gds))
            schematic.write_text('* Retained C++ transmitter storage\n' + geometry['schematic'])
            command(directory, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'lvs'])
            result['lvs'] = check_lvs(directory / 'lvs/storage.lvsdb', top.name, set(ports), count)
            command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                '--topcell', top.name, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
            drc = directory / 'drc' / ('storage_' + top.name + '_full.lyrdb')
            result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
            violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
            minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
            if set(violations) - minima:
                raise ValueError(case + ' storage geometry/maximum-density violations: ' + str(violations))
            result.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
            tap = [inst for inst in top.each_inst() if inst.cell.name == geometry['substrate_tap']['cell']]
            if len(tap) != 1:
                raise ValueError('ambiguous substrate fault insertion')
            tap_cell, tap_transform = tap[0].cell_index, tap[0].trans
            tap[0].delete()
            missing_tap = directory / 'missing_tap.gds'
            layout.write(str(missing_tap))
            command(directory, 'missing-tap-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', missing_tap,
                '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'missing-tap-lvs'])
            try:
                check_lvs(directory / 'missing-tap-lvs/missing_tap.lvsdb', top.name, set(ports), count)
            except ValueError:
                result['missing_substrate_tap_rejected'] = True
            else:
                raise ValueError('LVS accepted a floating storage substrate')
            top.insert(db.CellInstArray(tap_cell, tap_transform))
            # Keep all row wires while removing the first actual capacitor bank.
            victim = [inst for inst in top.each_inst() if inst.cell.name == geometry['components'][0]['name']]
            if len(victim) != 1:
                raise ValueError('ambiguous storage fault insertion')
            victim[0].delete()
            broken = directory / 'missing_storage.gds'
            layout.write(str(broken))
            command(directory, 'missing-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
                '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'missing-lvs'])
            try:
                check_lvs(directory / 'missing-lvs/missing_storage.lvsdb', top.name, set(ports), count)
            except ValueError:
                result['missing_storage_rejected'] = True
            else:
                raise ValueError('LVS accepted missing transmitter storage')
            report['stage'] = case + ':extraction'
            save()
            extraction = extract_named(gds, top.name, ports, pdk, args.magic, directory / 'extraction', substrate='VSS')
            result['extraction'] = extraction
            hashes.update(extraction['source_sha256'])
            if extraction['inventory']['models'] != {'cap_cmim': count}:
                raise ValueError('extracted storage inventory differs from physical capacitors')
            physical = directory / 'extraction/conserved.spice'
            result['connections'] = check_capacitor_connections(physical.read_text(), ports)
            result['artifact_sha256'] = {str(p): digest(p) for p in (gds, schematic, physical, drc,
                directory / 'lvs/storage.lvsdb', broken, directory / 'missing-lvs/missing_storage.lvsdb',
                missing_tap, directory / 'missing-tap-lvs/missing_tap.lvsdb')}
            result['status'] = 'pass'
            save()
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('transmitter storage inputs changed')
        report.update(status='pass', stage='complete')
        print('PASS transmitter main/history/output storage geometry, missing-storage rejection and conserved RC')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'exporter', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
