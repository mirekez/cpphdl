#!/usr/bin/env python3
"""Check compact PFD geometry, strict LVS, a cut clock input and named RC extraction."""
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
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from reference_clock_cell_layout import create_detector
from reference_clock_divider_layout import create_divider
from named_node_extraction import extract_named
from extract_analog import digest
from extracted_connections import check_capacitor_connections
from checks import check_drc_completion, check_lvs
from tool_process import configure_cleanup, execute


def run(args):
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    cells = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    paths = [SOURCE, SOURCE.parents[1] / 'clock_cell_layout.py', SOURCE.parents[1] / 'clock_cell_library.py',
        SOURCE.parents[1] / 'reference_clock_cell_layout.py',
        SOURCE.parents[1] / 'reference_clock_divider_layout.py',
        SOURCE.parents[1] / 'reference_clock_divider_cells.py',
        SOURCE.parents[1] / 'reference_clock_cells.py', SOURCE.parents[1] / 'named_node_extraction.py',
        SOURCE.parents[1] / 'extract_analog.py', SOURCE.parents[1] / 'extracted_connections.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.tools.resolve() / 'root/usr/bin/klayout', cells / 'gds/sg13g2_stdcell.gds',
        cells / 'lef/sg13g2_stdcell.lef', cells / 'spice/sg13g2_stdcell.spice']
    paths += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in paths}
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        module=args.module, scope='Clock macro local geometry, strict LVS and named conserved RC; excludes chip density/fill, field calibration and electrical qualification')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
               PDK_ROOT=str(pdk), PDK='ihp-sg13g2', OMP_NUM_THREADS='1')

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
        layout, top, geometry = (create_detector if args.module == 'detector' else create_divider)(pdk)
        count = geometry['normalized_device_count']
        clock_pin = 'REF' if args.module == 'detector' else 'CLOCK'
        report['geometry'] = geometry
        gds, schematic = work / 'detector.gds', work / 'detector.cir'
        layout.write(str(gds))
        schematic.write_text('* Intended PDK-cell detector circuit\n' + geometry['schematic'])
        command('lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        report['lvs'] = check_lvs(work / 'lvs/detector.lvsdb', top.name, set(geometry['ports']), count)
        if report['lvs']['instantiated_devices'] != count:
            raise ValueError('unexpected normalized detector device count')
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', top.name,
            '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / ('drc/detector_' + top.name + '_full.lyrdb')
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'")
                             for item in ET.parse(drc).getroot().find('items'))
        minimum_density = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - minimum_density:
            raise ValueError('PFD geometry/maximum-density violation: ' + str(violations))
        report.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations),
                      full_drc_pass=not violations)
        route = db.Box(*geometry['tracks'][clock_pin])
        selected = [s for s in top.each_shape(layout.layer(30, 0)) if s.is_box() and s.box == route]
        if len(selected) != 1:
            raise ValueError('ambiguous reference-input route for fault test')
        selected[0].delete()
        broken = work / 'cut_reference.gds'
        layout.write(str(broken))
        command('cut-input-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
            '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'cut-lvs'])
        try:
            check_lvs(work / 'cut-lvs/cut_reference.lvsdb', top.name, set(geometry['ports']), count)
        except ValueError:
            report['cut_reference_rejected'] = True
        else:
            raise ValueError('LVS accepted disconnected reference input')
        extracted = extract_named(gds, top.name, geometry['ports'], pdk, args.magic,
                                  work / 'extraction', substrate='VSS')
        report['extraction'] = extracted
        hashes.update(extracted['source_sha256'])
        text = (work / 'extraction/conserved.spice').read_text()
        report['connections'] = check_capacitor_connections(text, geometry['ports'])
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('PFD physical inputs changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in (gds, schematic, drc,
            work / 'lvs/detector.lvsdb', work / 'extraction/conserved.spice')}
        report['status'] = 'pass'
        print('PASS compact ' + args.module + ' local DRC/LVS/cut-clock and conserved named extraction')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', choices=('detector', 'divider'), default='detector')
    for name in ('pdk', 'tools', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
