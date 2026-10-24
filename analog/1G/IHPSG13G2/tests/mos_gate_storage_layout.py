"""Check actual MOSCAP banks and reject a severed gate strap."""
import argparse
from collections import Counter
import hashlib
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
from mos_gate_storage_layout import append_hv_storage_bank
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
    pcell = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    tracked = [SOURCE, SOURCE.parents[1] / 'mos_gate_storage_layout.py',
               SOURCE.parents[1] / 'mos_layout.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
               args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for parent in (technology, pcell) for p in parent.rglob('*')
                if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', cases={}, work=str(work), source_sha256=hashes,
                  full_chip_drc=False, scope='HV MOS gate-storage geometry and strict connectivity with unmodified PDK decks; isolated density and complete transmitter remain unqualified')
    environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), env=environment, stdout=log, stderr=-2, timeout=300,
                    memory_exclusive=False)

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)

    try:
        lvs_runner = technology / 'lvs/run_lvs.py'
        for label, copies, damage in (('unit', 1, None), ('two_hundred_twenty_cells', 220, None),
                                       ('open_gate_strap', 220, 'gate'), ('open_sub_via', 220, 'sub')):
            case = work / label
            case.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            top, info = append_hv_storage_bank(layout, args.pdk, 'StorageBank', copies)
            if damage == 'gate':
                top.shapes(layout.layer(info['terminals']['G']['layer'], 0)).clear()
            elif damage == 'sub':
                layout.cell('StorageBank_unit').shapes(layout.layer(29, 0)).clear()
            gds, netlist = case / 'device.gds', case / 'device.cir'
            layout.write(str(gds))
            netlist.write_text('* Every intended MOSCAP is explicit\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, lvs_runner, '--layout', gds,
                '--netlist', netlist, '--topcell', 'StorageBank', '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs_path = case / 'lvs/device.lvsdb'
            if damage:
                try:
                    check_lvs(lvs_path, 'StorageBank', {'G', 'SUB'}, 1)
                except ValueError:
                    report['cases'][label] = dict(expected_open_strap_rejected=True)
                else:
                    raise ValueError('LVS accepted a bank with disconnected physical plates')
            else:
                lvs = check_lvs(lvs_path, 'StorageBank', {'G', 'SUB'}, 1)
                command(case, 'drc', [sys.executable, technology / 'drc/run_drc.py', '--path', gds,
                    '--topcell', 'StorageBank', '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc_path = case / 'drc/device_StorageBank_full.lyrdb'
                completion = check_drc_completion(drc_path, technology / 'drc')
                violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc_path).getroot().find('items'))
                density_rules = {'AFil.g', 'AFil.g1', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
                if set(violations) - density_rules:
                    raise ValueError('local MOSCAP bank geometry violation: ' + str(violations))
                report['cases'][label] = dict(geometry=info, lvs=lvs, drc_completion=completion,
                    local_drc_violations=0, full_drc_pass=not violations, global_density_violations=dict(violations),
                    artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs_path, drc_path)})
            save()
            print(label + ': pass', flush=True)
        for value in (0, -1, True, 221):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_hv_storage_bank(layout, args.pdk, 'Invalid', value)
            except ValueError:
                continue
            raise ValueError('invalid MOSCAP bank count was accepted')
        if any(digest(p) != sha for p, sha in hashes.items()):
            raise ValueError('bank layout, checker or PDK changed during qualification')
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
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
