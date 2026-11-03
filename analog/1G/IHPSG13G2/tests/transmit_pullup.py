#!/usr/bin/env python3
"""Check the 50-ohm TX pullup's real contacts, parallel sections and wiring."""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_resistor_layout import append_transmit_pullup
from extract_analog import digest, extract
from resistor_array_extraction import adapt
from extracted_connections import check_capacitor_connections
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.ngspice.resolve(), args.tools.resolve() / 'root/usr/bin/klayout',
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', models / '.spiceinit']
    paths += [SOURCE.parents[1] / name for name in ('transmit_resistor_layout.py', 'resistor_layout.py',
        'mos_layout.py', 'extract_analog.py', 'extracted_connections.py', 'resistor_array_extraction.py',
        'resistor_body_model.py')]
    pcell = pdk / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    paths += [p for parent in (tech, models, pcell) for p in parent.rglob('*') if p.is_file()
              and p.suffix in ('.py', '.json', '.lvs', '.drc', '.spice', '.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='parallel pullup local geometry, strict LVS and TT extracted DC at 3.3 V supply; excludes chip fill, mismatch, PVT, self-heating and EM')
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
        layout = db.Layout()
        layout.dbu = .001
        top, geometry = append_transmit_pullup(layout, pdk, 'TransmitPullup')
        report['geometry'] = geometry
        gds, schematic = work / 'pullup.gds', work / 'pullup.cir'
        layout.write(str(gds))
        schematic.write_text('* Independent eight-section intended resistor and guard topology\n' + geometry['schematic'])
        command('lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        report['lvs'] = check_lvs(work / 'lvs/pullup.lvsdb', top.name, {'PLUS', 'MINUS', 'SUB'}, 2)
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', top.name,
            '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / 'drc/pullup_TransmitPullup_full.lyrdb'
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'")
                             for item in ET.parse(drc).getroot().find('items'))
        minimum_density = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - minimum_density:
            raise ValueError('pullup geometry or maximum-density violation: ' + str(violations))
        report.update(local_drc_violations=0, unfilled_minimum_density_violations=dict(violations),
                      full_drc_pass=not violations)
        # Remove one body and guard while retaining the entire collector metal.
        list(top.each_inst())[-1].delete()
        broken = work / 'missing_section.gds'
        layout.write(str(broken))
        command('missing-section-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
            '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'missing-lvs'])
        try:
            check_lvs(work / 'missing-lvs/missing_section.lvsdb', top.name, {'PLUS', 'MINUS', 'SUB'}, 2)
        except ValueError:
            report['missing_section_rejected'] = True
        else:
            raise ValueError('LVS accepted a missing parallel resistor section')
        result = extract(gds, top.name, ['PLUS', 'MINUS', 'SUB'], pdk, args.magic,
                         work / 'extraction', substrate='SUB')
        if result['conserved']['models'] != {'rppd': 8}:
            raise ValueError('extraction changed the eight parallel resistor bodies')
        ext = (work / 'extraction/ExtractedAnalog.ext').read_text()
        ports = re.findall(r'^port "([^"]+)" \d+ [-\d ]+ (\w+)$', ext, re.M)
        if sorted(ports) != [('MINUS', 'm6'), ('PLUS', 'm6'), ('SUB', 'm2')]:
            raise ValueError('duplicate, missing or wrong-layer pullup external port')
        physical, bodies = adapt((work / 'extraction/capacitance.spice').read_text(),
            (work / 'extraction/conserved.spice').read_text(), ext,
            (work / 'extraction/ExtractedAnalog.res.ext').read_text(), ['PLUS', 'MINUS', 'SUB'], 'SUB')
        report['resistor_body_partition'] = bodies
        report['connections'] = check_capacitor_connections(physical, ['PLUS', 'MINUS', 'SUB'])
        deck = ('* Actual extracted 50-ohm pullup under line-current bias\n'
            f'.lib "{models / "models/cornerRES.lib"}" res_typ\n.temp 27\n' + physical + '\n'
            'VPLUS PLUS 0 3.3\nVMINUS MINUS 0 3.3\nXdevice PLUS MINUS 0 ExtractedAnalog\n'
            '.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
            'dc VMINUS 1.3 3.3 .1\nwrdata resistance.dat v(PLUS) v(MINUS) i(VPLUS)\nquit\n.endc\n.end\n')
        (work / 'bench.cir').write_text(deck)
        if command('ngspice', [args.ngspice.resolve(), '-b', 'bench.cir']):
            raise ValueError('extracted pullup DC simulation failed')
        rows = [[float(v) for v in line.split()]
                for line in (work / 'resistance.dat').read_text().splitlines()[1:]]
        if len(rows) != 21 or any(len(row) != 4 or not all(math.isfinite(v) for v in row) for row in rows):
            raise ValueError('incomplete pullup current sweep')
        resistance = [(row[1] - row[2]) / -row[3] for row in rows if row[1] - row[2] > .05]
        error = max(abs(r / 50 - 1) for r in resistance)
        report['dc'] = dict(resistance_min_Ohm=min(resistance), resistance_max_Ohm=max(resistance),
            maximum_relative_error=error, relative_error_limit=.05,
            maximum_current_A=max(-row[3] for row in rows))
        if error > .05:
            raise ValueError('extracted pullup resistance differs from 50 ohms by more than 5%')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('pullup qualification inputs changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in (gds, schematic, drc,
            work / 'lvs/pullup.lvsdb', work / 'bench.cir', work / 'resistance.dat')}
        report['status'] = 'pass'
        print('PASS parallel 50-ohm pullup: local DRC/LVS, missing-section rejection and extracted DC')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'ngspice', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
