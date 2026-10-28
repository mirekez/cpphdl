#!/usr/bin/env python3
"""Independently check oscillator passive geometry, including open connections."""
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
from oscillator_passive_layout import append_one_ff, append_fine_resistor
from extract_analog import digest
from input_charge import verify_files
from checks import check_lvs, check_drc_completion
from tool_process import execute, configure_cleanup


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout'
    files = [SOURCE, SOURCE.parents[1] / 'oscillator_passive_layout.py',
        SOURCE.parents[1] / 'capacitor_layout.py', SOURCE.parents[1] / 'mos_layout.py',
        SOURCE.parents[1] / 'extract_analog.py', SOURCE.parents[1] / 'input_charge.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    files += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in files}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={}, full_chip_drc=False,
        scope='actual oscillator passive topology and geometry; excludes extracted electrical qualification and chip fill')
    environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), env=environment, stdout=log, stderr=-2,
                timeout=600, memory_exclusive=False).returncode

    save()
    try:
        for name, make, ports, count in [('Storage', append_one_ff, {'PLUS', 'MINUS'}, 3),
                ('FineResistance', append_fine_resistor, {'PLUS', 'MINUS', 'SUB'}, 64)]:
            case = work / name
            case.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            top, info = make(layout, args.pdk.resolve(), name)
            gds, netlist = case / 'passive.gds', case / 'passive.cir'
            layout.write(str(gds))
            netlist.write_text('* Intended passive dimensions, not extracted from layout\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', name, '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs = case / 'lvs/passive.lvsdb'
            result = check_lvs(lvs, name, ports, count)
            command(case, 'drc', [sys.executable, tech / 'tech/drc/run_drc.py', '--path', gds,
                '--topcell', name, '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
            drc = case / f'drc/passive_{name}_full.lyrdb'
            completion = check_drc_completion(drc, tech / 'tech/drc')
            violations = Counter(i.findtext('category').strip("'") for i in ET.parse(drc).getroot().find('items'))
            density = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
            if set(violations) - density:
                raise ValueError('passive local geometry violations: ' + str(violations))
            # Break series-cap junction vias or resistor series joins while
            # retaining the independent schematic and external pin labels.
            top.shapes(layout.layer(125 if name == 'Storage' else 10, 0)).clear()
            damaged = case / 'open.gds'
            layout.write(str(damaged))
            command(case, 'open_lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', damaged,
                '--netlist', netlist, '--topcell', name, '--run_mode', 'deep', '--run_dir', case / 'open_lvs'])
            try:
                check_lvs(case / 'open_lvs/open.lvsdb', name, ports, count)
            except ValueError:
                pass
            else:
                raise ValueError('LVS accepted an open physical passive connection')
            report['cases'][name] = dict(geometry=info, lvs=result, drc_completion=completion,
                drc_violations=dict(violations), full_drc_pass=not violations, open_connection_rejected=True,
                artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs, drc, damaged)})
            save()
            print(name + ': LVS/local DRC and open-connection rejection pass', flush=True)
        verify_files(hashes)
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
