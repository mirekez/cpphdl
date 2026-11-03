"""Check the cold receiver sampler's local geometry and missing-charge rejection."""
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
from receive_sampler_layout import create_receive_sampler, PORTS, TOP
from extract_analog import digest
from hardware import source_dependencies
from input_charge import verify_files
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute, memory_slot


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    paths = [SOURCE, args.exporter.resolve(), tools / 'root/usr/bin/klayout', ROOT / '1G/tests/hardware.py']
    paths += [SOURCE.parents[1] / name for name in (
        'receive_sampler_layout.py', 'receive_sampler_shields.py', 'mos_layout.py', 'capacitor_layout.py', 'capacitor_series_layout.py',
        'oscillator_passive_layout.py', 'reference_clock_row.py', 'processing_clock_gate_layout.py',
        'extract_analog.py', 'input_charge.py')]
    paths += [ROOT / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    paths += [p for parent in (tech, tech.parent / 'python/sg13g2_pycell_lib')
              for p in parent.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.drc', '.lvs')]
    hashes = {str(p): digest(p) for p in paths}
    hashes.update(source_dependencies([SOURCE.with_name('receive_sampler_export.cpp')],
        ROOT.parent, args.cxx, ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        full_chip_drc=False, electrical_qualified=False,
        scope='actual cold receive sampler local DRC/LVS and missing-device checks; '
              'fill, extracted acquisition/hold, parent loading and full receiver remain required')
    env = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'],
               OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(directory, label, argv):
        report['stage'] = directory.name + ':' + label
        save()
        with (directory / (label + '.log')).open('w') as log:
            result = execute(['prlimit', '--as=1073741824', '--', *map(str, argv)],
                cwd=directory, env=env, stdout=log, stderr=-2, timeout=900, memory_exclusive=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(label + ' execution failed: ' + str(result.returncode))
        return result.returncode

    try:
        save()
        with memory_slot(['receive-sampler-local-geometry'], exclusive=not args.concurrent):
            if command(work, 'export', [args.exporter.resolve(), work / 'export']):
                raise ValueError('C++ sampler export failed')
            source, manifest = work / 'export/sampler.va', work / 'export/sampler.json'
            hashes.update({str(p): digest(p) for p in (source, manifest)})
            for name, missing in (('connected', None), ('missing_hold', 'SamplerStorage7'),
                                  ('missing_dummy_n', 'SamplerMOS5'), ('missing_dummy_p', 'SamplerMOS6')):
                directory = work / name
                directory.mkdir()
                layout, top, geometry = create_receive_sampler(pdk, source, manifest)
                if missing:
                    victim, = [i for i in top.each_inst() if i.cell.name == missing]
                    victim.delete()
                gds, schematic = directory / 'sampler.gds', directory / 'sampler.cir'
                layout.write(str(gds))
                schematic.write_text(geometry['schematic'])
                command(directory, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                    '--netlist', schematic, '--topcell', TOP, '--run_mode', 'deep', '--run_dir', directory / 'lvs'])
                lvs = directory / 'lvs/sampler.lvsdb'
                if not lvs.is_file():
                    raise ValueError('LVS produced no comparison database; inspect ' + str(directory / 'lvs.log'))
                artifacts = [gds, schematic, lvs]
                result = report['cases'][name] = {}
                if missing:
                    try:
                        check_lvs(lvs, TOP, set(PORTS), 6)
                    except ValueError:
                        result['missing_device_rejected'] = True
                    else:
                        raise ValueError('LVS accepted ' + name)
                else:
                    result.update(geometry=geometry, lvs=check_lvs(lvs, TOP, set(PORTS), 6))
                    command(directory, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                        '--topcell', TOP, '--run_dir', directory / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                    drc = directory / ('drc/sampler_' + TOP + '_full.lyrdb')
                    result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
                    violations = Counter(item.findtext('category').strip("'")
                        for item in ET.parse(drc).getroot().find('items'))
                    minima = {'AFil.g', 'AFil.g1', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j',
                              'M5.j', 'TM1.c', 'TM2.c'}
                    result['drc_violations'] = dict(violations)
                    if set(violations) - minima:
                        raise ValueError('sampler geometry/maximum-density violations: ' + str(violations))
                    result.update(local_drc_pass=True, retained_minimum_density_violations=dict(violations))
                    artifacts.append(drc)
                result.update(status='pass', artifact_sha256={str(p): digest(p) for p in artifacts})
                save()
        verify_files(hashes)
        report.update(status='pass', stage='complete')
        print('PASS sampler local DRC/LVS and independent missing hold/dummy rejection')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exporter', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--concurrent', action='store_true', help='use a separately budgeted 1 GiB local geometry slot')
    run(parser.parse_args())
