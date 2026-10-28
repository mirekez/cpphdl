"""Check HV storage plate access, including deliberately broken adapter paths."""
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
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from mos_gate_storage_adapter import append_hv_storage_adapter
from extract_analog import digest
from input_charge import verify_files
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    pcell = tech.parent / 'python/sg13g2_pycell_lib'
    paths = [SOURCE, tools / 'root/usr/bin/klayout']
    paths += [SOURCE.parents[1] / name for name in ('mos_gate_storage_adapter.py', 'mos_gate_storage_layout.py',
        'mos_layout.py', 'extract_analog.py', 'input_charge.py')]
    paths += [ROOT / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    paths += [p for parent in (tech, pcell) for p in parent.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.drc', '.lvs')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={}, full_chip_drc=False,
        scope='220-unit HV storage with physical plate-access adapter; local geometry and broken-access rejection; '
              'global density, extracted charge and loaded replica qualification remain open')
    env = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'])

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(directory, label, argv):
        report['stage'] = directory.name + ':' + label
        save()
        with (directory / (label + '.log')).open('w') as log:
            result = execute(['prlimit', '--as=1073741824', '--', *map(str, argv)],
                cwd=directory, env=env, stdout=log, stderr=-2, timeout=600, memory_exclusive=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(label + ' execution failed')

    try:
        save()
        for case, cut in (('connected', None), ('open_gate_access', 126), ('open_ground_vias', 133)):
            directory = work / case
            directory.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            top, info = append_hv_storage_adapter(layout, pdk, 'HVStorageAdapter')
            if cut is not None:
                top.shapes(layout.layer(cut, 0)).clear()
            gds, netlist = directory / 'adapter.gds', directory / 'adapter.cir'
            layout.write(str(gds))
            netlist.write_text(info['schematic'])
            command(directory, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'lvs'])
            lvs_path = directory / 'lvs/adapter.lvsdb'
            result = report['cases'][case] = {}
            if cut is not None:
                try:
                    check_lvs(lvs_path, top.name, {'PLUS', 'MINUS'}, 1)
                except ValueError:
                    result['broken_access_rejected'] = True
                else:
                    raise ValueError('LVS accepted disconnected HV storage access')
                artifacts = (gds, netlist, lvs_path)
            else:
                result.update(geometry=info, lvs=check_lvs(lvs_path, top.name, {'PLUS', 'MINUS'}, 1))
                command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                    '--topcell', top.name, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc = directory / 'drc/adapter_HVStorageAdapter_full.lyrdb'
                result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
                violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
                density = {'AFil.g', 'AFil.g1', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j',
                           'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
                if set(violations) - density:
                    raise ValueError('HV storage adapter local geometry violations: ' + str(violations))
                result.update(local_drc_violations=0, global_density_violations=dict(violations))
                artifacts = (gds, netlist, lvs_path, drc)
            result['artifact_sha256'] = {str(p): digest(p) for p in artifacts}
            result['status'] = 'pass'
            save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS HV storage adapter local geometry and broken-access rejection')
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
