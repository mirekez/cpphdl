#!/usr/bin/env python3
"""Check the C++ transmitter translator physical topology and extracted wiring."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_translator_layout import PORTS, create_transmit_translator
from named_node_extraction import extract_named
from extracted_connections import check_capacitor_connections
from extract_analog import digest
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    paths = [SOURCE, args.exporter.resolve(), args.tools.resolve() / 'root/usr/bin/klayout', models / '.spiceinit']
    paths += [SOURCE.parents[1] / name for name in ('transmit_translator_layout.py', 'reference_clock_row.py',
        'reference_clock_placement.py',
        'processing_clock_gate_layout.py', 'resistor_layout.py', 'mos_layout.py',
        'extract_analog.py', 'named_node_extraction.py', 'extracted_connections.py',
        'capacitor_layout.py', 'core_driver_process.py')]
    paths += [SOURCE.with_name('transmit_translator_export.cpp'), ANALOG / '1G/clock_level_shifter.hpp',
        ANALOG / 'basic/cascoded_level_shifter.hpp', ANALOG / 'basic/inverter.hpp']
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    pcell = pdk / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    paths += [p for parent in (tech, models, pcell) for p in parent.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc', '.spice', '.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='C++ translator topology, local DRC/LVS, missing-device rejection and conserved RC; electrical/full-TX/chip qualification separate')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
        PDK_ROOT=str(pdk), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=work, env=env, stdout=log, stderr=-2,
                           timeout=300, memory_exclusive=False).returncode

    try:
        save()
        if command('export', [args.exporter.resolve(), work / 'export']):
            raise ValueError('C++ translator export failed')
        source = work / 'export/translator.va'
        hashes[str(source)] = digest(source)
        layout, top, geometry = create_transmit_translator(pdk, source, args.storage)
        report['geometry'] = geometry
        minimum_devices = sum(geometry['intended_device_inventory'].values())
        gds, schematic = work / 'translator.gds', work / 'translator.cir'
        layout.write(str(gds))
        schematic.write_text('* C++ translator process geometry with explicit MIM storage\n' + geometry['schematic'])
        command('lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        report['lvs'] = check_lvs(work / 'lvs/translator.lvsdb', top.name, set(PORTS), minimum_devices)
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', top.name,
            '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / 'drc/translator_TransmitTranslator_full.lyrdb'
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
        minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - minima:
            raise ValueError('translator geometry/maximum-density violations: ' + str(violations))
        report.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
        # Remove a real translator MOS while retaining its surrounding conductors.
        victim = [inst for inst in top.each_inst() if inst.cell.name == 'TranslatorMOS0']
        if len(victim) != 1:
            raise ValueError('ambiguous translator-device fault insertion')
        victim[0].delete()
        broken = work / 'missing_device.gds'
        layout.write(str(broken))
        command('missing-device-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
            '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'missing-lvs'])
        try:
            check_lvs(work / 'missing-lvs/missing_device.lvsdb', top.name, set(PORTS), minimum_devices)
        except ValueError:
            report['missing_device_rejected'] = True
        else:
            raise ValueError('LVS accepted missing translator MOS')
        extraction = extract_named(gds, top.name, PORTS, pdk, args.magic,
                                   work / 'extraction', substrate='VSS')
        report['extraction'] = extraction
        hashes.update(extraction['source_sha256'])
        physical = (work / 'extraction/conserved.spice').read_text()
        report['connections'] = check_capacitor_connections(physical, PORTS)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('translator inputs changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in (gds, schematic, drc,
            work / 'lvs/translator.lvsdb', source, work / 'extraction/conserved.spice')}
        report['status'] = 'pass'
        print('PASS TX translator: local DRC/LVS, missing-device rejection and conserved RC')
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
    parser.add_argument('--storage', choices=('declared', 'psp-charge'), default='declared')
    run(parser.parse_args())
