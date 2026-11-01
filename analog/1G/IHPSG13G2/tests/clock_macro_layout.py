#!/usr/bin/env python3
"""Verify native clock IC pin access inside the analog row router."""
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
from clock_macro_layout import append_clock_macro
from reference_clock_row import create_row
from named_node_extraction import extract_named
from extract_analog import digest
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    cells = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    paths = [SOURCE, args.tools.resolve() / 'root/usr/bin/klayout']
    paths += [SOURCE.parents[1] / name for name in ('clock_macro_layout.py', 'reference_clock_row.py',
        'processing_clock_gate_layout.py', 'clock_cell_layout.py', 'clock_cell_library.py',
        'reference_clock_cell_layout.py', 'reference_clock_cells.py', 'reference_clock_divider_layout.py',
        'reference_clock_divider_cells.py', 'named_node_extraction.py', 'extract_analog.py')]
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    paths += [cells / name for name in ('gds/sg13g2_stdcell.gds', 'lef/sg13g2_stdcell.lef', 'spice/sg13g2_stdcell.spice')]
    paths += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='native detector/divider access in analog row: local geometry, strict LVS, cut-clock rejection and conserved extraction; no connected PLL electrical qualification')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
               PDK_ROOT=str(pdk), PDK='ihp-sg13g2', OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(directory, label, argv):
        with (directory / (label + '.log')).open('w') as log:
            # Density-only DRC and deliberate cut-LVS runs can return nonzero.
            # Their complete persisted reports determine acceptance below.
            execute(list(map(str, argv)), cwd=directory, env=env, stdout=log, stderr=-2,
                    timeout=300, memory_exclusive=False)

    try:
        save()
        for kind in ('detector', 'divider'):
            directory = work / kind
            directory.mkdir()
            ports = ('VDD VSS REF FEEDBACK CLEAR UP UP_N DOWN DOWN_N RESET_B' if kind == 'detector'
                     else 'VDD VSS CLOCK CLEAR Q Q_N').split()

            def parts(layout):
                cell, info = append_clock_macro(layout, pdk, kind.title() + 'Native', kind)
                return [(cell, info, {port: port for port in ports}, 'macro')]

            layout, top, geometry = create_row(kind.title() + 'Integration', ports, parts,
                order_parts=lambda parts, dbu: ([0], {'component_order': [parts[0][0].name]}))
            count = geometry['components'][0]['geometry']['normalized_device_count']
            result = dict(status='running', geometry=geometry)
            report['cases'][kind] = result
            gds, netlist = directory / 'clock.gds', directory / 'clock.cir'
            layout.write(str(gds))
            netlist.write_text('* Native clock physical access topology\n' + geometry['schematic'])
            command(directory, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'lvs'])
            result['lvs'] = check_lvs(directory / 'lvs/clock.lvsdb', top.name, set(ports), count)
            if result['lvs']['instantiated_devices'] != count:
                raise ValueError('native macro access changes physical devices')
            command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                '--topcell', top.name, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
            drc = directory / ('drc/clock_' + top.name + '_full.lyrdb')
            result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
            violations = Counter(item.findtext('category').strip("'")
                                 for item in ET.parse(drc).getroot().find('items'))
            minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
            if set(violations) - minima:
                raise ValueError('native macro access violates geometry: ' + str(violations))
            result.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
            clock = 'REF' if kind == 'detector' else 'CLOCK'
            access = [p for p in geometry['escapes'] if p['net'] == clock]
            if len(access) != 1:
                raise ValueError('ambiguous macro clock access')
            x = round(access[0]['location_um'][0] / layout.dbu)
            wires = [s for s in top.each_shape(layout.layer(50, 0))
                     if s.is_box() and s.box.center().x == x and s.box.height() > 1000]
            if len(wires) != 1:
                raise ValueError('ambiguous clock column for cut fault')
            wires[0].delete()
            cut = directory / 'cut_clock.gds'
            layout.write(str(cut))
            command(directory, 'cut-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', cut,
                '--netlist', netlist, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'cut-lvs'])
            try:
                check_lvs(directory / 'cut-lvs/cut_clock.lvsdb', top.name, set(ports), count)
            except ValueError:
                result['cut_clock_rejected'] = True
            else:
                raise ValueError('LVS accepted a disconnected macro clock')
            result['extraction'] = extract_named(gds, top.name, ports, pdk, args.magic,
                                                directory / 'extraction', substrate='VSS')
            hashes.update(result['extraction']['source_sha256'])
            result['artifact_sha256'] = {str(p): digest(p) for p in
                (gds, netlist, drc, directory / 'lvs/clock.lvsdb', directory / 'extraction/conserved.spice')}
            result['status'] = 'pass'
            save()
            print(kind, 'macro access pass', flush=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('native macro access inputs changed')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
