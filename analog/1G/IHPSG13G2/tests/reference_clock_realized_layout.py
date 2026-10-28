#!/usr/bin/env python3
"""Verify the PLL with native clock ICs, retained analog devices and wiring."""
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
from reference_clock_realized_layout import create_realized_reference_clock
from reference_clock_layout_process import PORTS
from reference_clock_cells import CELLS as DETECTOR_CELLS
from reference_clock_divider_cells import CELLS as DIVIDER_CELLS
from reference_clock_realization import BLOCKS
from lvs_view import create_flat_view
from extract_analog import digest
from hardware import source_dependencies
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute

EXPECTED = dict(sg13_lv_nmos=85, sg13_lv_pmos=71, sg13_hv_nmos=5,
                sg13_hv_pmos=4, cap_cmim=68, rppd=8, dpantenna=1, ntap1=16, ptap1=1)
MINIMUM_DENSITY = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
TOP = 'RealizedReferenceClock'


def check_inventory(path):
    result = check_lvs(path, TOP, set(PORTS), sum(EXPECTED.values()))
    comparison = db.LayoutVsSchematic()
    comparison.read(str(path))
    circuit = comparison.netlist().circuit_by_name(TOP)
    inventory = Counter(d.device_class().name.lower() for d in circuit.each_device())
    if dict(inventory) != EXPECTED or list(circuit.each_subcircuit()):
        raise ValueError('realized PLL normalized physical inventory changed')
    result['normalized_device_inventory'] = dict(inventory)
    return result


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    names = ('reference_clock_realized_layout.py', 'reference_clock_realization.py',
        'clock_macro_layout.py', 'reference_clock_cell_layout.py', 'reference_clock_cells.py',
        'reference_clock_divider_layout.py', 'reference_clock_divider_cells.py',
        'clock_cell_layout.py', 'clock_cell_library.py', 'reference_clock_row.py',
        'reference_clock_layout.py', 'reference_clock_layout_process.py', 'reference_clock_process.py',
        'reference_clock_placement.py', 'processing_clock_gate_layout.py', 'processing_clock_gate_process.py',
        'analog_row_fold.py', 'antenna_layout.py', 'capacitor_bank_layout.py', 'capacitor_layout.py',
        'capacitor_series_layout.py', 'mos_layout.py', 'oscillator_passive_layout.py',
        'resistor_layout.py', 'core_driver_process.py', 'mos_geometry.py', 'lvs_view.py', 'extract_analog.py')
    files = [SOURCE, args.exporter.resolve(), tools / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    files += [SOURCE.parents[1] / name for name in names]
    files += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py', 'fill_core.py')]
    files += [library / name for name in ('spice/sg13g2_stdcell.spice', 'gds/sg13g2_stdcell.gds', 'lef/sg13g2_stdcell.lef')]
    files += [p for p in tech.parent.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc', '.lym')]
    hashes = {str(p): digest(p) for p in files}
    hashes.update(source_dependencies([SOURCE.with_name('reference_clock_layout_export.cpp')], ANALOG.parent,
        args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        filled=not args.unfilled, full_chip_drc=False,
        scope='composed PLL geometry and explicit native-IC process realization; '
              'electrical PEX, corners and full-chip qualification remain separate')
    env = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
        PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'], PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
        REPEATER_PDK=str(pdk), REPEATER_TOP=TOP, REPEATER_SOURCE=str(ANALOG / 'pdk/1G_repeater_IHPSG13G2'))
    local_pins = {p for spec in dict(DETECTOR_CELLS, **DIVIDER_CELLS).values() for p in spec[0].split()}
    local_pins |= {p for spec in BLOCKS.values() for p in spec[0].split()}
    local_pins.add('RESET_B')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        report['stage'] = label
        save()
        with (work / (label + '.log')).open('w') as log:
            result = execute(['prlimit', '--as=' + str((1 if args.unfilled else 3) * 1024 ** 3),
                '--', *map(str, argv)], env=env, stdout=log, stderr=-2, timeout=1800,
                memory_exclusive=not args.unfilled)
        report[label + '_exit'] = result.returncode

    def lvs(label, gds, netlist):
        view = work / ('lvs_input/' + gds.name)
        report[label + '_view'] = create_flat_view(gds, view, TOP, local_pins)
        command(label, [sys.executable, tech / 'lvs/run_lvs.py', '--layout', view,
            '--netlist', netlist, '--topcell', TOP, '--run_mode', 'flat', '--run_dir', work / label])
        return work / label / (gds.stem + '.lvsdb')

    try:
        save()
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work], stdout=log, stderr=-2, check=True,
                    timeout=60, memory_exclusive=False)
        layout, top, geometry = create_realized_reference_clock(pdk, work / 'reference.va', work / 'realizations.json')
        report['geometry'] = geometry
        unfilled, netlist, gds = work / 'clock_unfilled.gds', work / 'clock.cir', work / 'clock.gds'
        layout.write(str(unfilled))
        netlist.write_text(geometry['schematic'])
        if args.unfilled:
            gds = unfilled
        else:
            command('fill', [tools / 'bin/klayout', '-zz', '-r',
                ANALOG / 'pdk/1G_repeater_IHPSG13G2/fill_core.py', '-rd', 'output_file=' + str(gds), unfilled])
            if report['fill_exit'] != 0 or not gds.is_file():
                raise ValueError('native PLL fill failed')
        result = lvs('lvs', gds, netlist)
        report['lvs'] = check_inventory(result)
        if report['lvs_exit'] != 0:
            raise ValueError('PLL LVS process failed')
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', TOP,
                       '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / 'drc' / (gds.stem + '_' + TOP + '_full.lyrdb')
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
        report['drc_violations'] = dict(violations)
        if set(violations) - (MINIMUM_DENSITY if args.unfilled else set()):
            raise ValueError('PLL design rules failed: ' + str(violations))
        if not args.unfilled and report['drc_exit'] != 0:
            raise ValueError('filled PLL DRC process failed')
        report.update(local_drc_pass=True, full_drc_pass=not args.unfilled and not violations)
        # Break the actual reference-clock M3/M4 connection in the verified GDS.
        layout = db.Layout()
        layout.read(str(gds))
        top = layout.cell(TOP)
        access = [p for p in geometry['escapes'] if p['kind'] == 'macro' and p['net'] == 'REF']
        if len(access) != 1:
            raise ValueError('ambiguous native PLL reference-clock branch')
        x, y = (round(v / layout.dbu) for v in access[0]['location_um'])
        vias = [s for s in top.each_shape(layout.layer(49, 0)) if s.bbox().center() == db.Point(x, y)]
        if len(vias) != 1:
            raise ValueError('ambiguous PLL clock via for fault injection')
        vias[0].delete()
        damaged = work / 'disconnected_reference.gds'
        layout.write(str(damaged))
        broken = lvs('cut_lvs', damaged, netlist)
        try:
            check_inventory(broken)
        except ValueError:
            report['disconnected_reference_rejected'] = True
        else:
            raise ValueError('LVS accepted disconnected PLL reference clock')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('PLL or PDK inputs changed during geometry verification')
        report.update(status='pass', stage='complete', artifact_sha256={str(p): digest(p) for p in
            (gds, unfilled, netlist, result, drc, damaged, broken, work / 'reference.va', work / 'realizations.json')})
        print('PASS', 'unfilled local' if args.unfilled else 'filled', 'native PLL DRC/LVS and open-reference rejection', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'work', 'exporter'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--unfilled', action='store_true', help='diagnostic local DRC only; density fill remains required')
    run(parser.parse_args())
