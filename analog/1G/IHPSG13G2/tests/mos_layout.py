#!/usr/bin/env python3
"""Require all real MOS fingers and substrate contacts to match the schematic."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from mos_layout import append_hv_nmos, append_hv_pmos, append_lv_nmos, append_lv_pmos
from mos_bank_layout import append_hv_nmos_bank
from checks import check_lvs, check_drc_completion


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    root_work = args.work.resolve()
    root_work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root_work))
    technology = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech'
    environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
    tracked = [SOURCE, SOURCE.parents[1] / 'mos_layout.py', SOURCE.parents[1] / 'mos_bank_layout.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
               technology / 'lvs/run_lvs.py', technology / 'drc/run_drc.py',
               args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for p in technology.rglob('*') if p.is_file() and p.suffix in ('.lvs', '.drc', '.json')]
    pcell = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    tracked += [p for p in pcell.rglob('*') if p.is_file() and p.suffix in ('.py', '.json')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', work=str(work), cases={}, full_chip_drc=False, source_sha256=hashes,
        selected_cases=args.case or ['baseline'],
        scope='strict-port transistor/tap LVS and complete local DRC tables; unfilled global density remains failing')
    destination = work / 'checks.json'

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        destination.write_text(payload)
        (root_work / 'checks.json').write_text(payload)

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            result = subprocess.run(list(map(str, argv)), env=environment, stdout=log, stderr=-2, timeout=120)
        return result.returncode

    try:
        # Five-micron TX fingers keep the real guarded active region below
        # the maximum density limit; total electrical width is unchanged.
        for label, fingers, width, damage in (('one', 1, 10, False), ('four', 4, 40, False),
                                             ('sample_n', 5, 25, False), ('sample_p', 5, 50, False),
                                             ('dummy_n', 2, 12.5, False), ('dummy_p', 5, 25, False),
                                             ('pmos', 4, 40, False), ('lv_nmos', 4, 40, False),
                                             ('lv_pmos', 4, 40, False),
                                             ('lv_narrow_n', 1, 1, False), ('lv_narrow_p', 1, 1, False),
                                             ('tx_steering', 80, 400, False), ('tx_sink', 90, 450, False),
                                             ('tx_cascode', 300, 1500, False), ('tx_history_cascode', 100, 500, False),
                                             ('tx_history_steering', 40, 200, False), ('tx_history_sink', 30, 150, False),
                                             ('tx_missing_bank', 300, 1500, True),
                                             ('open_gate', 4, 40, True)):
            if args.case and label not in args.case:
                continue
            if not args.case and label.startswith('tx_'):
                continue
            case = work / label
            case.mkdir(exist_ok=True)
            layout = db.Layout()
            layout.dbu = .001
            make = {'pmos': append_hv_pmos, 'sample_p': append_hv_pmos,
                    'dummy_p': append_hv_pmos, 'lv_nmos': append_lv_nmos,
                    'lv_pmos': append_lv_pmos, 'lv_narrow_n': append_lv_nmos,
                    'lv_narrow_p': append_lv_pmos}.get(label, append_hv_nmos)
            if label.startswith('tx_'):
                make = append_hv_nmos_bank
            top, info = make(layout, args.pdk, 'RoutedMOS', width, .13 if label.startswith('lv_') else .45, fingers)
            if label == 'tx_missing_bank':
                # Leave the intended 1500 um aggregate MOS untouched in the
                # schematic, but remove one physical 500 um bank and its tap.
                list(top.each_inst())[-1].delete()
            elif damage:
                # Open the last physical gate connection while keeping all
                # port labels and the independent source netlist unchanged.
                vias = list(top.each_shape(layout.layer(19, 0)))
                gate = max((s for s in vias if s.bbox().center().y < 0), key=lambda s: s.bbox().center().x)
                gate.delete()
            gds, netlist = case / 'device.gds', case / 'device.cir'
            layout.write(str(gds))
            netlist.write_text('* Independent intended MOS and explicit PCell substrate contact\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, technology / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', 'RoutedMOS', '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs = case / 'lvs/device.lvsdb'
            if damage:
                try:
                    check_lvs(lvs, 'RoutedMOS', {'D', 'G', 'S', 'B'}, 2)
                except ValueError:
                    key = 'expected_missing_bank_rejected' if label == 'tx_missing_bank' else 'expected_open_gate_rejected'
                    report['cases'][label] = {key: True}
                else:
                    raise ValueError('LVS accepted a damaged physical transistor')
            else:
                result = check_lvs(lvs, 'RoutedMOS', {'D', 'G', 'S', 'B'}, 2)
                command(case, 'drc', [sys.executable, technology / 'drc/run_drc.py', '--path', gds,
                    '--topcell', 'RoutedMOS', '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc = case / 'drc/device_RoutedMOS_full.lyrdb'
                completion = check_drc_completion(drc, technology / 'drc')
                root = ET.parse(drc).getroot()
                categories = {c.findtext('name') for c in root.find('categories').iter('category')}
                if len(categories) < 800 or not {'Ant.a', 'Ant.g', 'Pin.a'}.issubset(categories):
                    raise ValueError('incomplete local DRC/antenna/pin rule inventory')
                violations = Counter(i.findtext('category').strip("'") for i in root.find('items'))
                density_rules = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
                if set(violations) - density_rules:
                    raise ValueError('local MOS geometry, antenna or pin violation: ' + str(violations))
                report['cases'][label] = dict(geometry=info, lvs=result, drc_completion=completion,
                    local_drc_violations=0, full_drc_pass=not violations,
                    unfilled_global_density_violations=dict(violations),
                    artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs, drc)})
            save()
            print(label + ': pass', flush=True)
        for width, length, fingers in ((0, .45, 1), (10, .44, 1), (11, .45, 1),
                                       (20.001, .45, 2), (25, .45, 3), (10, .45, 0)):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_hv_nmos(layout, args.pdk, 'Invalid', width, length, fingers)
            except ValueError:
                continue
            raise ValueError('invalid MOS geometry was accepted')
        for width, fingers in ((400, 40), (1500, 301), (0, 0), (5, True),
                               (5005, 1001), (float('nan'), 80)):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_hv_nmos_bank(layout, args.pdk, 'InvalidBank', width, .45, fingers)
            except ValueError:
                continue
            raise ValueError('invalid parallel MOS bank geometry was accepted')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('MOS layout/checker/PDK sources changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--case', action='append', choices=('one', 'four', 'sample_n', 'sample_p',
        'dummy_n', 'dummy_p', 'pmos', 'lv_nmos', 'lv_pmos', 'lv_narrow_n', 'lv_narrow_p',
        'tx_steering', 'tx_sink', 'tx_cascode', 'tx_history_cascode',
        'tx_history_steering', 'tx_history_sink', 'tx_missing_bank', 'open_gate'))
    run(parser.parse_args())
