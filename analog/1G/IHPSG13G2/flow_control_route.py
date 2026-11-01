#!/usr/bin/env python3
"""Route, extract and replay the placed eight-SRAM PAUSE datapath."""
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
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup, execute
from checks import check_timing


def run(args):
    placement = json.loads(args.placement.read_text())
    cells = json.loads(args.cells.read_text())
    if placement.get('status') != 'placement_checkpoint_only' or cells.get('status') != 'pass':
        raise ValueError('qualified mapped cells and placement checkpoint required')
    hashes = {}
    for proof in (placement, cells):
        for key in ('source_sha256', 'artifact_sha256'):
            verify_files(proof[key])
            hashes.update(proof[key])
    if placement['source_sha256'].get(str(args.cells.resolve())) != digest(args.cells):
        raise ValueError('placement used another mapped proof')
    tools, pdk = args.tools.resolve(), args.pdk.resolve()
    macro = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
    models = [pdk / ('ihp-sg13g2/libs.ref/sg13g2_sram/verilog/' + n) for n in
              (macro + '.v', 'RM_IHPSG13_2P_core_behavioral_bm_bist_ideal.v')]
    files = [SOURCE, SOURCE.with_suffix('.tcl'), SOURCE.with_name('flow_control_clock_loads.tcl'),
        SOURCE.with_name('flow_control_signal_loads.tcl'),
        args.placement.resolve(), args.cells.resolve(),
        COMMON / 'tool_process.py', COMMON / 'checks.py', COMMON / 'clean_empty_nets.tcl',
        COMMON / 'routing_rc_margin.tcl',
        tools / 'bin/openroad', tools / 'rcx_patterns.rules', args.verilator.resolve(),
        ANALOG / '1G/tests/flow_controlled_repeater.cpp', *models]
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='routed SRAM/control datapath, extracted synchronous timing and functional replay; '
              'DRC/LVS, CDC and complete-chip assembly are separate')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv, timeout, exclusive):
        report['stage'] = label
        save()
        with (work / (label + '.log')).open('w') as log:
            execute(argv, cwd=work, env=environment, stdout=log, stderr=-2,
                    check=True, timeout=timeout, memory_exclusive=exclusive)

    save()
    try:
        for name in ('placed.odb', 'constraints.sdc', 'route_tech.lef'):
            shutil.copyfile(Path(placement['work']) / name, work / name)
        environment = dict(os.environ, FLOW_WORK=str(work), FLOW_PDK=str(pdk), FLOW_TOOLS=str(tools),
                           FLOW_COMMON=str(COMMON), OMP_NUM_THREADS='1')
        for stage, timeout in [('global', 3600), ('detail', 7200), ('extract', 1800)]:
            environment['FLOW_STAGE'] = stage
            command(stage, [tools / 'bin/openroad', '-no_init', '-exit', SOURCE.with_suffix('.tcl')], timeout, True)
        report['extracted_repairs'] = []
        for attempt in range(2):
            if 'VIOLATED' not in (work / 'electrical.rpt').read_text():
                break
            retained = work / ('before-eco-' + str(attempt + 1))
            retained.mkdir()
            for name in ('routed.odb', 'core.spef', 'postroute.v', 'setup.rpt', 'hold.rpt',
                         'electrical.rpt', 'timing.rpt', 'route_drc.rpt'):
                shutil.copyfile(work / name, retained / name)
            report['extracted_repairs'].append(dict(
                attempt=attempt + 1,
                before_sha256={str(p): digest(p) for p in retained.iterdir()}))
            for stage, timeout in [('eco', 1800), ('detail', 7200), ('extract', 1800)]:
                environment['FLOW_STAGE'] = stage
                command(stage + '-' + str(attempt + 1),
                        [tools / 'bin/openroad', '-no_init', '-exit', SOURCE.with_suffix('.tcl')], timeout, True)
        report['artifact_sha256'] = {str(work / n): digest(work / n) for n in
            ('routed.odb', 'core.def', 'core.spef', 'core.cdl', 'postroute.v', 'setup.rpt', 'hold.rpt', 'electrical.rpt')}
        save()
        objects = work / 'replay_obj'
        command('build-replay', ['prlimit', '--as=1610612736', '--', args.verilator.resolve(),
            '--cc', '--exe', '--build', '--no-skip-identical', '-j', '1', '--top-module', 'FlowControlledRepeater',
            '-Wno-fatal', '-DFUNCTIONAL', '-DSYNTHESIS', '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17',
            work / 'postroute.v', Path(cells['work']) / 'cells.v', *models,
            ANALOG / '1G/tests/flow_controlled_repeater.cpp', '-MAKEFLAGS',
            'CXX=' + args.cxx + ' LINK=' + args.cxx], 1200, True)
        command('replay', [objects / 'VFlowControlledRepeater'], 300, False)
        result = (work / 'replay.log').read_text().strip()
        if result != cells['result']:
            raise ValueError('routed PAUSE datapath changed the independently checked frame trajectory')
        report['result'] = result
        report['timing'] = check_timing(work)
        verify_files(hashes)
        report.update(status='routed_timing_replay_pass', stage='complete')
        print(result)
        print(json.dumps(report['timing']))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('placement', 'cells', 'pdk', 'tools', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
