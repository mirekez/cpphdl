"""Qualify contacted poly geometry separately from its nominal resistance model."""
import argparse
from collections import Counter
import hashlib
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
from resistor_layout import append_rppd
from checks import check_drc_completion, check_lvs
from tool_process import configure_cleanup, execute


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    technology = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    pcell = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    paths = [SOURCE, SOURCE.parents[1] / 'resistor_layout.py', SOURCE.parents[1] / 'mos_layout.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.tools.resolve() / 'root/usr/bin/klayout', args.ngspice.resolve(), Path(sys.executable).resolve(), models / '.spiceinit']
    paths += [p for parent in (technology, pcell, models) for p in parent.rglob('*') if p.is_file()
              and p.suffix in ('.py', '.json', '.lvs', '.drc', '.lib', '.spice', '.osdi')]
    hashes = {str(path): digest(path) for path in paths}
    report = dict(status='running', cases={}, source_sha256=hashes, work=str(work), full_chip_drc=False,
        scope='strict resistor/tap connectivity, complete local DRC and nominal low-field resistance; no assembled AFE, PVT, mismatch or EM qualification')
    environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
        PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=case, env=environment, stdout=log, stderr=-2,
                           timeout=300, memory_exclusive=False).returncode

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)

    try:
        for label, value, width, damage in (('line_100', 100, 20, False), ('bias_2500', 2500, 2, False),
                ('bias_100k', 100000, 2, False), ('open_contacts', 2500, 2, True)):
            case = work / label
            case.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            top, info = append_rppd(layout, args.pdk, 'PolyResistor', value, width)
            if damage:
                top.shapes(layout.layer(19, 0)).clear()
            gds, schematic = case / 'device.gds', case / 'device.cir'
            layout.write(str(gds))
            schematic.write_text('* Independent resistor and substrate-tap dimensions\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, technology / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', schematic, '--topcell', 'PolyResistor', '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs_path = case / 'lvs/device.lvsdb'
            if damage:
                try:
                    check_lvs(lvs_path, 'PolyResistor', {'PLUS', 'MINUS', 'SUB'}, 2)
                except ValueError:
                    report['cases'][label] = dict(expected_open_contacts_rejected=True)
                else:
                    raise ValueError('LVS accepted resistor ports disconnected from their poly and substrate')
            else:
                lvs = check_lvs(lvs_path, 'PolyResistor', {'PLUS', 'MINUS', 'SUB'}, 2)
                command(case, 'drc', [sys.executable, technology / 'drc/run_drc.py', '--path', gds,
                    '--topcell', 'PolyResistor', '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc_path = case / 'drc/device_PolyResistor_full.lyrdb'
                completion = check_drc_completion(drc_path, technology / 'drc')
                violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc_path).getroot().find('items'))
                density_rules = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
                if set(violations) - density_rules:
                    raise ValueError('local poly resistor violation: ' + str(violations))
                deck = ('Low-field resistance from the actual r3_cmc process model\n'
                    f'.lib "{models / "models/cornerRES.lib"}" res_typ\n.temp 27\n'
                    'Vtest PLUS 0 0\n' + info['spice_instance'].replace('MINUS', '0').replace('SUB', '0') + '\n'
                    '.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
                    'dc Vtest -.1 .1 .05\nwrdata resistance.dat v(PLUS) i(Vtest)\nquit\n.endc\n.end\n')
                (case / 'model.cir').write_text(deck)
                status = command(case, 'ngspice', [args.ngspice.resolve(), '-b', 'model.cir'])
                if status or re.search(r'\b(error|fatal|failed|aborted)\b|not found', (case / 'ngspice.log').read_text(), re.I):
                    raise ValueError('process resistor DC experiment failed')
                rows = [[float(v) for v in line.split()] for line in (case / 'resistance.dat').read_text().splitlines()[1:]]
                if len(rows) != 5 or any(len(row) != 3 or not all(math.isfinite(v) for v in row) for row in rows):
                    raise ValueError('missing process resistor DC coverage')
                measured = [-row[1] / row[2] for row in rows if abs(row[1]) > .01]
                error = max(abs(value / info['pcell_nominal_resistance_Ohm'] - 1) for value in measured)
                if error > .005 or any(value <= 0 for value in measured):
                    raise ValueError('process resistance differs from its nominal geometry: ' + str(error))
                report['cases'][label] = dict(geometry=info, lvs=lvs, drc_completion=completion,
                    local_drc_violations=0, global_density_violations=dict(violations), full_drc_pass=not violations,
                    measured_low_field_resistance_Ohm=measured, maximum_relative_resistance_error=error,
                    artifact_sha256={str(p): digest(p) for p in (gds, schematic, lvs_path, drc_path,
                        case / 'model.cir', case / 'resistance.dat', case / 'ngspice.log')})
            save()
            print(label + ': pass', flush=True)
        for value, width in ((0, 2), (-1, 2), (float('nan'), 2), (float('inf'), 2),
                             (100, 1), (100, 2.005), (1e9, 2)):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_rppd(layout, args.pdk, 'Invalid', value, width)
            except ValueError:
                continue
            raise ValueError('invalid or unrealizable resistor was accepted')
        if any(digest(path) != expected for path, expected in hashes.items()):
            raise ValueError('resistor layout, process model, tool or checker changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
