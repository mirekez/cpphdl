#!/usr/bin/env python3
"""Run complete geometry checks on the routed SRAM/control macro."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
COMMON = ANALOG / 'pdk/1G_repeater_IHPSG13G2'
sys.path[:0] = [str(SOURCE.parent), str(COMMON)]
from checks import check_drc, check_lvs
from extract_analog import digest
from flow_control_stream import stream, TOP, MACRO
from input_charge import verify_files
from tool_process import configure_cleanup, execute


def run(args):
    routed = json.loads(args.routed.read_text())
    if routed.get('status') != 'routed_timing_replay_pass':
        raise ValueError('passing routed timing and native/cell replay required')
    hashes = {}
    for key in ('source_sha256', 'artifact_sha256'):
        verify_files(routed[key])
        hashes.update(routed[key])
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    libraries = pdk / 'ihp-sg13g2/libs.ref'
    technology = pdk / 'ihp-sg13g2/libs.tech/klayout'
    files = [SOURCE, SOURCE.with_name('flow_control_stream.py'), args.routed.resolve(),
             COMMON / 'checks.py', COMMON / 'fill_core.py', COMMON / 'tool_process.py',
             tools / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    files += [p for p in technology.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc', '.lym', '.lyt', '.map')]
    for family, cell in [('sg13g2_stdcell', 'sg13g2_stdcell'), ('sg13g2_sram', MACRO)]:
        files += [libraries / family / suffix / (cell + '.' + suffix) for suffix in ('gds', 'lef', 'cdl')]
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='SRAM/control macro physical checks; extracted CDC, post-fill RC and full chip remain separate')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(name, argv, timeout):
        report['stage'] = name
        save()
        with (work / (name + '.log')).open('w') as log:
            result = execute(['prlimit', '--as=4294967296', '--', *argv], env=environment,
                stdout=log, stderr=-2, timeout=timeout, memory_exclusive=True)
        report[name + '_exit_code'] = result.returncode
        save()
        return result.returncode

    save()
    try:
        origin = Path(routed['work'])
        for name in ('core.def', 'route_tech.lef', 'core.cdl', 'postroute.v'):
            shutil.copyfile(origin / name, work / name)
        environment = dict(os.environ, REPEATER_PDK=str(pdk), REPEATER_TOP=TOP, REPEATER_SOURCE=str(COMMON),
                           PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'], MALLOC_ARENA_MAX='2')
        report['stream'] = stream(pdk, work)
        gds = work / 'flow_control.gds'
        if command('fill', [tools / 'bin/klayout', '-zz', '-r', COMMON / 'fill_core.py', '-rd',
                           'output_file=' + str(gds), work / 'flow_control_unfilled.gds'], 3600):
            raise ValueError('SRAM/control fill failed')
        command('drc', [sys.executable, technology / 'tech/drc/run_drc.py', '--path', gds,
                       '--topcell', TOP, '--run_dir', work / 'drc', '--mp', '1', '--density_thr', '1', '--antenna'], 7200)
        errors = []
        try:
            report['drc'] = check_drc(work / ('drc/flow_control_' + TOP + '_full.lyrdb'), technology / 'tech/drc')
            if report['drc_exit_code']:
                raise ValueError('DRC tool did not exit successfully')
        except Exception as error:
            errors.append('DRC: ' + str(error))
        (work / 'lvs.cir').write_text('\n'.join('.include "' + str(p) + '"' for p in
            (libraries / 'sg13g2_stdcell/cdl/sg13g2_stdcell.cdl', libraries / ('sg13g2_sram/cdl/' + MACRO + '.cdl'),
             work / 'core.cdl')) + '\n')
        command('lvs', [sys.executable, technology / 'tech/lvs/run_lvs.py', '--layout', gds, '--netlist', work / 'lvs.cir',
                       '--topcell', TOP, '--run_mode', 'deep', '--run_dir', work / 'lvs'], 7200)
        try:
            import re
            cdl = (work / 'core.cdl').read_text().replace('\n+', ' ')
            ports = set(re.search(r'(?im)^\.subckt\s+' + TOP + r'\s+([^\n]+)', cdl)[1].split())
            report['lvs'] = check_lvs(work / 'lvs/flow_control.lvsdb', TOP, ports)
            if report['lvs_exit_code']:
                raise ValueError('LVS tool did not exit successfully')
        except Exception as error:
            errors.append('LVS: ' + str(error))
        report['artifact_sha256'] = {str(p): digest(p) for p in (gds, work / 'core.def', work / 'core.cdl', work / 'lvs.cir')}
        verify_files(hashes)
        if errors:
            raise ValueError('; '.join(errors))
        report.update(status='macro_geometry_pass', stage='complete')
        print('PASS routed SRAM/control DRC and transistor LVS')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('routed', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
