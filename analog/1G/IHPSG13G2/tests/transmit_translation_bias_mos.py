"""Check the MOS-storage replica's real wiring and common substrate by LVS."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / '1G/tests'),
               str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_translation_bias_mos_layout import create_mos_translation_bias
from transmit_translation_bias_layout import PORTS
from extract_analog import digest
from hardware import source_dependencies
from input_charge import verify_files
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    pcell = tech.parent / 'python/sg13g2_pycell_lib'
    paths = [SOURCE, args.exporter.resolve(), tools / 'root/usr/bin/klayout', ROOT / '1G/tests/hardware.py']
    paths += [SOURCE.parents[1] / name for name in (
        'transmit_translation_bias_mos_layout.py', 'transmit_translation_bias_layout.py',
        'mos_gate_storage_adapter.py', 'mos_gate_storage_layout.py', 'mos_layout.py',
        'resistor_layout.py', 'reference_clock_row.py', 'processing_clock_gate_layout.py',
        'core_driver_process.py', 'extract_analog.py', 'input_charge.py')]
    paths += [ROOT / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    paths += [p for parent in (tech, pcell) for p in parent.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.drc', '.lvs')]
    hashes = {str(p): digest(p) for p in paths}
    hashes.update(source_dependencies([SOURCE.with_name('transmit_translation_bias_export.cpp')],
        ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='export-bound HV-storage replica local geometry and missing-device rejection; '
              'charge, extracted startup, loaded bank, fill and full chip remain unqualified',
        full_chip_drc=False, electrical_qualified=False, cases={})
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
        return result.returncode

    try:
        if command(work, 'export', [args.exporter.resolve(), work / 'export']):
            raise ValueError('replica C++ export failed')
        source = work / 'export/translation_bias.va'
        hashes[str(source)] = digest(source)
        for case, missing in (('connected', None), ('missing_storage', 'ReplicaMOSStorage'),
                              ('missing_replica', 'ReplicaMOS')):
            directory = work / case
            directory.mkdir()
            layout, top, info = create_mos_translation_bias(pdk, source)
            if missing is not None:
                victim, = [instance for instance in top.each_inst() if instance.cell.name == missing]
                victim.delete()
            gds, netlist = directory / 'replica.gds', directory / 'replica.cir'
            layout.write(str(gds))
            netlist.write_text(info['schematic'])
            command(directory, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', directory / 'lvs'])
            lvs = directory / 'lvs/replica.lvsdb'
            result = report['cases'][case] = {}
            artifacts = [gds, netlist, lvs]
            if missing is not None:
                try:
                    check_lvs(lvs, top.name, set(PORTS), 5)
                except ValueError:
                    result['missing_device_rejected'] = True
                else:
                    raise ValueError('LVS accepted ' + case)
            else:
                result.update(geometry=info, lvs=check_lvs(lvs, top.name, set(PORTS), 5))
                command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                    '--topcell', top.name, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc = directory / 'drc/replica_TransmitTranslationBias_full.lyrdb'
                result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
                violations = Counter(item.findtext('category').strip("'")
                    for item in ET.parse(drc).getroot().find('items'))
                minima = {'AFil.g', 'AFil.g1', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j',
                          'M5.j', 'TM1.c', 'TM2.c'}
                if set(violations) - minima:
                    raise ValueError('replica geometry/maximum-density violations: ' + str(violations))
                result.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
                artifacts.append(drc)
            result.update(status='pass', artifact_sha256={str(p): digest(p) for p in artifacts})
            save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS MOS-storage replica local geometry, shared substrate and missing-device rejection')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'exporter', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
