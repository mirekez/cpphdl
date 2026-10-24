"""Check parallel storage and reject a bank whose physical plate strap is cut."""
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
from capacitor_bank_layout import append_mim_bank
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
    tracked = [SOURCE, SOURCE.parents[1] / 'capacitor_bank_layout.py',
               SOURCE.parents[1] / 'capacitor_layout.py', SOURCE.parents[1] / 'mos_layout.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
               args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for parent in (technology, pcell) for p in parent.rglob('*')
                if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', cases={}, work=str(work), source_sha256=hashes,
                  full_chip_drc=False, scope='parallel MIM local geometry and strict connectivity; isolated density remains unqualified')
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
        for label, storage, damage in (('ten_pf', 10e-12, False), ('twenty_pf', 20e-12, False),
                                       ('fifty_pf', 50e-12, False), ('four_hundred_pf', 400e-12, False),
                                       ('open_top_strap', 10e-12, True)):
            case = work / label
            case.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            top, info = append_mim_bank(layout, args.pdk, 'StorageBank', storage)
            if storage >= 20e-12 and max(info['width_um'], info['height_um']) / min(info['width_um'], info['height_um']) > 1.5:
                raise ValueError('large storage bank is not compact')
            if damage:
                top.shapes(layout.layer(info['terminals']['PLUS']['layer'], 0)).clear()
            gds, netlist = case / 'device.gds', case / 'device.cir'
            layout.write(str(gds))
            netlist.write_text('* Every intended parallel capacitor is explicit\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, technology / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', 'StorageBank', '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs_path = case / 'lvs/device.lvsdb'
            if damage:
                try:
                    check_lvs(lvs_path, 'StorageBank', {'PLUS', 'MINUS'}, 1)
                except ValueError:
                    report['cases'][label] = dict(expected_open_strap_rejected=True)
                else:
                    raise ValueError('LVS accepted a bank with disconnected physical plates')
            else:
                lvs = check_lvs(lvs_path, 'StorageBank', {'PLUS', 'MINUS'}, 1)
                command(case, 'drc', [sys.executable, technology / 'drc/run_drc.py', '--path', gds,
                    '--topcell', 'StorageBank', '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc_path = case / 'drc/device_StorageBank_full.lyrdb'
                completion = check_drc_completion(drc_path, technology / 'drc')
                violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc_path).getroot().find('items'))
                density_rules = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
                # This large reference is deliberately a negative chip-budget
                # case. Keep the foundry's global recommended MIM-area failure;
                # never present it as a fully DRC-clean candidate.
                over_budget = label == 'four_hundred_pf'
                if over_budget and violations.get('MIM.gR') != 1:
                    raise ValueError('oversized MIM bank did not trigger the chip-area rule')
                if set(violations) - density_rules - ({'MIM.gR'} if over_budget else set()):
                    raise ValueError('local capacitor-bank geometry violation: ' + str(violations))
                report['cases'][label] = dict(geometry=info, lvs=lvs, drc_completion=completion,
                    local_drc_violations=0, full_drc_pass=not violations, global_density_violations=dict(violations),
                    expected_chip_mim_budget_failure=over_budget,
                    artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs_path, drc_path)})
            save()
            print(label + ': pass', flush=True)
        for value in (0, -1e-12, float('nan'), float('inf'), 513e-12):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_mim_bank(layout, args.pdk, 'Invalid', value)
            except ValueError:
                continue
            raise ValueError('invalid bank storage was accepted')
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
