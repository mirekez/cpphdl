#!/usr/bin/env python3
"""Implement verified receive DSP/timing hardware as an actual SG13G2 module."""
import argparse
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import sys
import tempfile
import time
from contextlib import nullcontext

SOURCE = Path(__file__).resolve().parent
ANALOG = SOURCE.parents[1]
COMMON = ANALOG / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
sys.path.insert(0, str(SOURCE.parent / 'tests'))
from checks import check_drc, check_lvs, check_timing
from hardware import configure_cleanup, execute
from stream import stream
from route_checkpoints import retain_route_snapshots

MODULES = {
    'ReceiveFilter': dict(case='filter_datapath', die=1100, harness='filter_datapath.cpp',
                         fixture='filter-vectors.txt', defines='', clock_distance=275,
                         clock_branch_distance=1, capacitance_limit=.1,
                         route_setup_margin=.7, route_cap_margin=30, route_transition_target=.2),
    'PipelinedReceiveFilter': dict(case='filter_pipeline', die=1600, harness='filter.cpp',
                                  fixture='filter-vectors.txt', defines=' -DPIPELINED_FILTER',
                                  clock_distance=400, clock_branch_distance=1, capacitance_limit=.1),
    'ReceiveTimingLoop': dict(case='timing', die=400, harness='timing.cpp',
                             fixture='timing-vectors.txt', defines='', clock_distance=50, clock_branch_distance=25,
                             capacitance_limit=.05),
    'ReceiveSampleNormalizer': dict(case='normalizer', die=280, harness='normalizer.cpp', period=4,
                                   fixture=None, defines='', clock_distance=100, clock_branch_distance=25,
                                   capacitance_limit=.05, route_setup_margin=.2, route_cap_margin=30),
    'ADCReceiveController': dict(case='adc_receive', die=500, harness='adc_receive.cpp', period=4,
                                fixture=None, defines='', clock_distance=100, clock_branch_distance=25,
                                capacitance_limit=.25, preparation_capacitance=.18,
                                optimization_capacitance=.22, analog_control_load=.16,
                                output_buffer_cell='sg13g2_buf_4',
                                route_setup_margin=.2, route_cap_margin=10),
    'PAM5DriverControl': dict(case='pam5_driver', die=200, harness='pam5_driver.cpp', period=8,
                            fixture=None, defines='', clock_distance=25, clock_branch_distance=25,
                            capacitance_limit=.05),
}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def changed_inputs(expected, retained=None):
    """Check consumed copies of build artifacts and live design/tool inputs.

    Another regression may replace its build directory while routing runs.
    That cannot change the copies already consumed by this implementation.
    Missing or modified retained copies must still invalidate the result.
    """
    retained = retained or {}
    changed = []
    for name, value in expected.items():
        try:
            actual = digest(Path(retained.get(name, name)))
        except OSError:
            actual = None
        if actual != value:
            changed.append(name)
    return changed


def import_placement(origin, work, top, constraints):
    """Resume geometry, never a previous pass result or changed design inputs."""
    origin = origin.resolve()
    metadata = origin / 'physical_module.json'
    previous = json.loads(metadata.read_text())
    if previous.get('top') != top or previous.get('constraints') != constraints:
        raise ValueError('placement checkpoint has a different module or timing contract')
    inputs = previous.get('input_sha256', {})
    if not inputs:
        raise ValueError('placement checkpoint lacks input provenance')
    allowed = {SOURCE / 'physical_module.py', COMMON / 'tool_process.py', COMMON / 'route_extract.tcl',
               COMMON / 'route_checkpoints.py', COMMON / 'fill_core.py', COMMON / 'fill_remainder.py',
               COMMON / 'stream.py', COMMON / 'antenna_inputs.tcl'}
    changed = {}
    for name, expected in inputs.items():
        path = Path(name)
        actual = digest(path)
        if actual != expected:
            snapshot = origin / 'source_snapshot' / path.relative_to(ANALOG.parent) if path in allowed else None
            if snapshot is None or not snapshot.is_file() or digest(snapshot) != expected:
                raise ValueError('placement design/PDK/route input changed: ' + name)
            # These drivers or routing-only files cannot alter the saved
            # placement. Their new hashes are recorded and every downstream
            # route/function/timing/DRC/LVS check runs again.
            scope = ('routing-only' if path.name in ('route_extract.tcl', 'route_checkpoints.py', 'antenna_inputs.tcl') else
                     'postroute geometry' if path.name in ('fill_core.py', 'fill_remainder.py', 'stream.py') else
                     'driver/resourcing')
            changed[name] = dict(placement_sha256=expected, resume_sha256=actual, scope=scope)
    if digest(origin / 'mapped.v') != digest(work / 'mapped.v'):
        raise ValueError('placement checkpoint netlist differs from the current mapped proof')
    adopted = {}
    recorded = previous.get('placement_checkpoint_sha256', {})
    for name in ('placed.odb', 'placed.sdc'):
        path = origin / name
        value = digest(path)
        if name in recorded and recorded[name] != value:
            raise ValueError('placement checkpoint artifact changed: ' + name)
        shutil.copyfile(path, work / name)
        adopted[name] = value
    return dict(origin=str(origin), origin_report_sha256=digest(metadata),
                checkpoint_sha256=adopted, recorded_before_interruption=bool(recorded),
                changed_driver_sources=changed,
                scope='resume placement only; all routing, timing, functional, DRC and LVS checks run again')


def implement(args):
    top = getattr(args, 'module', 'ReceiveFilter')
    module_spec = MODULES[top]
    workers = getattr(args, 'workers', 1)
    route_threads = getattr(args, 'route_threads', 1)
    if route_threads not in (1, 2):
        raise ValueError('detailed routing supports one or two worker threads')
    if workers not in (1, 2):
        raise ValueError('physical DRC supports one or two workers')
    transition = getattr(args, 'max_transition', .5)
    if not math.isfinite(transition) or transition <= 0:
        raise ValueError('maximum accepted transition must be finite and positive')
    routed_checkpoint = getattr(args, 'resume_routed_database', None)
    if routed_checkpoint is not None and getattr(args, 'resume_placement', None) is not None:
        raise ValueError('choose either a placement checkpoint or a routed database')
    work, mapped = args.work.resolve(), args.mapped_work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('physical filter work directory must be fresh')
    report = dict(status='running', top=top, full_1000base_t_chip=False,
                  scope='standalone digital receive module; not an AFE, PHY or repeater chip', stages=[],
                  drc_workers=workers, route_threads=route_threads,
                  route_optimization=dict(transition_target_ns=module_spec.get('route_transition_target', .3),
                                          setup_margin_ns=module_spec.get('route_setup_margin', 0),
                                          capacitance_margin_percent=module_spec.get('route_cap_margin', 10)),
                  clock_tree=dict(buffer_distance_um=module_spec['clock_distance'],
                                  branch_buffer_min_distance_um=module_spec['clock_branch_distance'],
                                  sink_cluster_size=4, sink_cluster_max_diameter_um=40),
                  constraints=dict(period_ns=module_spec.get('period', 8), clock_uncertainty_ns=.1, input_output_delay_ns=.4,
                                   optimization_transition_ns=.3, accepted_transition_ns=transition,
                                   optimization_fanout=4, accepted_fanout=8,
                                   preparation_capacitance_pf=module_spec.get('preparation_capacitance', .01),
                                   optimization_capacitance_pf=module_spec.get('optimization_capacitance', .03),
                                   analog_control_load_pf=module_spec.get('analog_control_load', .005),
                                   accepted_capacitance_pf=module_spec['capacitance_limit']))

    def save():
        (work / 'physical_module.json').write_text(json.dumps(report, indent=2) + '\n')

    environment = dict(os.environ, REPEATER_PDK=str(args.pdk.resolve()), REPEATER_WORK=str(work),
                       REPEATER_SOURCE=str(COMMON), REPEATER_TOOLS=str(args.tools.resolve()),
                       REPEATER_TOP=top, REPEATER_DIE_SIZE=str(module_spec['die']),
                       REPEATER_CLOCK_DISTANCE=str(module_spec['clock_distance']),
                       REPEATER_CLOCK_BRANCH_DISTANCE=str(module_spec['clock_branch_distance']),
                       REPEATER_MAX_CAPACITANCE=str(module_spec['capacitance_limit']),
                       REPEATER_MAX_TRANSITION=str(transition),
                       REPEATER_PREPARATION_CAPACITANCE=str(module_spec.get('preparation_capacitance', .01)),
                       REPEATER_OPTIMIZATION_CAPACITANCE=str(module_spec.get('optimization_capacitance', .03)),
                       REPEATER_OUTPUT_BUFFER_CELL=module_spec.get('output_buffer_cell', ''),
                       REPEATER_ROUTE_SETUP_MARGIN=str(module_spec.get('route_setup_margin', 0)),
                       REPEATER_ROUTE_TRANSITION_TARGET=str(module_spec.get('route_transition_target', .3)),
                       REPEATER_ROUTE_CAP_MARGIN=str(module_spec.get('route_cap_margin', 10)),
                       REPEATER_ROUTE_THREADS=str(route_threads),
                       REPEATER_CONSTRAINTS=str(work / 'constraints.sdc'))
    environment['PATH'] = str(args.tools.resolve() / 'bin') + os.pathsep + environment.get('PATH', '')

    def stage(name, command, timeout=600, fatal=True):
        started = time.monotonic()
        print(name, flush=True)
        with (work / (name + '.log')).open('w') as log:
            with retain_route_snapshots(work) if name == 'route-extract' else nullcontext():
                result = execute([str(x) for x in command], cwd=work, env=environment,
                                 stdout=log, stderr=-2, timeout=timeout)
        report['stages'].append(dict(name=name, command=[str(x) for x in command], returncode=result.returncode,
                                     seconds=round(time.monotonic() - started, 3)))
        save()
        if result.returncode and fatal:
            raise RuntimeError(name + ' failed; inspect ' + str(work / (name + '.log')))
        return result.returncode

    save()
    try:
        evidence = json.loads((mapped / 'hardware_checks.json').read_text())
        if (evidence.get('status') != 'pass' or
                (top in ('ReceiveFilter', 'PipelinedReceiveFilter') and not evidence.get('native_frontend_exact')) or
                not evidence.get('native_mapped_exact') or evidence.get('module') != top or
                evidence.get('mapped_sha256') != digest(mapped / 'mapped.v')):
            raise ValueError('filter functional evidence is absent, failed or stale')
        if not evidence.get('source_sha256') or any(digest(Path(path)) != value
                for path, value in evidence['source_sha256'].items()):
            raise ValueError('filter source provenance is absent or stale')
        pdk, tools = args.pdk.resolve(), args.tools.resolve()
        pdk_python = pdk / '.cpphdl/venv/bin/python'
        if not pdk_python.is_file():
            raise ValueError('prepare the pinned IHP PDK first')
        module = json.loads((mapped / 'mapped.json').read_text())['modules'][top]
        ports = {'VDD', 'VSS'}
        for name, port in module['ports'].items():
            width = len(port['bits'])
            ports.update([name] if width == 1 else [f'{name}[{i}]' for i in range(width)])
        # Routing only needs the port names. Do not keep the large synthesis
        # database resident beside OpenROAD's routing database.
        del module
        input_names = ('mapped.v', 'mapped.json', 'cells.v', 'cpp_trace.csv', 'hardware_checks.json')
        if top == 'ADCReceiveController':
            if evidence.get('simulation_interface_sha256') != digest(mapped / 'mapped_interface.sv'):
                raise ValueError('ADC array-port simulation interface is absent or stale')
            input_names += ('mapped_interface.sv',)
        if module_spec['fixture'] is not None:
            input_names += (module_spec['fixture'],)
        for name in input_names:
            shutil.copyfile(mapped / name, work / name)
        report['retained_input_paths'] = {str(mapped / name): str(work / name) for name in input_names}
        report['input_sha256'] = {str(mapped / name): digest(work / name) for name in input_names}
        # Verify the copies, including the evidence, before any expensive stage.
        if (json.loads((work / 'hardware_checks.json').read_text()) != evidence or
                digest(work / 'mapped.v') != evidence['mapped_sha256'] or
                (top == 'ADCReceiveController' and digest(work / 'mapped_interface.sv') !=
                 evidence['simulation_interface_sha256'])):
            raise ValueError('mapped evidence changed while taking the implementation snapshot')
        report['input_sha256'].update(evidence['source_sha256'])
        for path in (tools / 'bin/openroad', tools / 'root/usr/bin/openroad', tools / 'bin/klayout',
                     tools / 'root/usr/bin/klayout', tools / 'rcx_patterns.rules',
                     Path(args.verilator), Path(args.cxx), pdk_python):
            resolved = path.resolve()
            report['input_sha256'][str(resolved)] = digest(resolved)
        for path in (SOURCE / 'physical_module.py',
                     SOURCE / 'extract_routed.tcl',
                     SOURCE.parent / 'tests' / module_spec['harness'], COMMON / 'place_route.tcl',
                     COMMON / 'route_extract.tcl', COMMON / 'antenna_inputs.tcl', COMMON / 'stream.py',
                     COMMON / 'route_checkpoints.py',
                     COMMON / 'clean_empty_nets.tcl', COMMON / 'tool_process.py',
                     COMMON / 'route_tech.py', COMMON / 'fill_core.py', COMMON / 'fill_remainder.py', COMMON / 'checks.py',
                     SOURCE.parent / 'tests/hardware.py'):
            report['input_sha256'][str(path)] = digest(path)
        tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
        library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
        for pattern in ('drc/**/*.drc', 'drc/**/*.json', 'lvs/**/*.lvs', 'macros/sg13g2_filler_*.lym'):
            for path in tech.glob(pattern):
                report['input_sha256'][str(path)] = digest(path)
        for path in library.rglob('*'):
            if path.is_file() and path.suffix in ('.lib', '.lef', '.gds', '.cdl'):
                report['input_sha256'][str(path)] = digest(path)
        # Keep the exact project sources used by an expensive run, even while
        # later development changes the working tree. The PDK is pinned/hashed.
        for source_path in report['input_sha256']:
            path = Path(source_path)
            if path.is_relative_to(ANALOG.parent) and not path.is_relative_to(pdk):
                if path.suffix in ('.cpp', '.hpp', '.h', '.py', '.tcl'):
                    snapshot = work / 'source_snapshot' / path.relative_to(ANALOG.parent)
                    snapshot.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, snapshot)
        (work / 'constraints.sdc').write_text(f'''create_clock -name sample_clock -period {module_spec.get('period', 8)} [get_ports clk]
set_clock_uncertainty 0.1 [get_clocks sample_clock]
set_input_delay 0.4 -clock sample_clock [all_inputs -no_clocks]
set_input_transition 0.1 [all_inputs]
set_output_delay 0.4 -clock sample_clock [all_outputs]
set_load 0.005 [all_outputs]
''')
        if top == 'ADCReceiveController':
            with (work / 'constraints.sdc').open('a') as constraints:
                constraints.write(f'''set analog_controls [get_ports {{trial_out* acquire_out* convert_out*}}]
if {{[llength $analog_controls] != 160}} {{ error "ADC analog-control interface is incomplete" }}
set_load {module_spec['analog_control_load']} $analog_controls
''')
        stage('routing-technology', [pdk_python, COMMON / 'route_tech.py', '--pdk', pdk, '--work', work])
        resume = getattr(args, 'resume_placement', None)
        if routed_checkpoint is not None:
            checkpoint = routed_checkpoint.resolve()
            expected = digest(checkpoint)
            shutil.copyfile(checkpoint, work / 'routed_checkpoint.odb')
            if digest(work / 'routed_checkpoint.odb') != expected:
                raise ValueError('routed checkpoint changed while being copied')
            report['input_sha256'][str(checkpoint)] = expected
            report['retained_input_paths'][str(checkpoint)] = str(work / 'routed_checkpoint.odb')
            report['resumed_routed_database'] = dict(origin=str(checkpoint), sha256=expected,
                scope='database only; recompute extraction, constraints and all functional/physical checks')
            save()
            stage('extract-checkpoint', [tools / 'bin/openroad', '-no_init', '-exit', SOURCE / 'extract_routed.tcl'], timeout=1800)
        elif resume is not None:
            report['resumed_placement'] = import_placement(resume, work, top, report['constraints'])
            for name in ('physical_module.json', 'placed.odb', 'placed.sdc'):
                path = resume.resolve() / name
                report['input_sha256'][str(path)] = digest(path)
        else:
            environment['REPEATER_STAGE'] = 'place'
            stage('place-clock-tree', [tools / 'bin/openroad', '-no_init', '-exit', COMMON / 'place_route.tcl'], timeout=1800)
        if routed_checkpoint is None:
            report['placement_checkpoint_sha256'] = {name: digest(work / name) for name in ('placed.odb', 'placed.sdc')}
            save()
            environment['REPEATER_STAGE'] = 'route'
            stage('route-extract', [tools / 'bin/openroad', '-no_init', '-exit', COMMON / 'place_route.tcl'],
                  timeout=43200 if top in ('ReceiveFilter', 'PipelinedReceiveFilter') else
                          5400 if top == 'ADCReceiveController' else 1800)
        # The exact independent oracle runs again on the routed netlist, using
        # the real Liberty-derived cell functions, before layout qualification.
        postroute_sources = [work / 'postroute.v']
        if top == 'ADCReceiveController':
            # The routed netlist keeps its physical flattened ports. Restore
            # the already checked public array interface only for simulation.
            text, count = re.subn(r'\bmodule\s+' + top + r'\s*\(',
                                 'module ' + top + '_mapped(', (work / 'postroute.v').read_text(), count=1)
            if count != 1:
                raise ValueError('routed ADC controller top was not found')
            (work / 'postroute_flat.v').write_text(text)
            postroute_sources = [work / 'postroute_flat.v', work / 'mapped_interface.sv']
        stage('postroute-build', [args.verilator, '--cc', '--exe', '--build', '--no-skip-identical',
              '--flatten', '--output-groups', '0', '--output-split', '10000', '--output-split-cfuncs', '500',
              '-j', '2', '--top-module', top, '-Wno-fatal', '--Mdir', work / 'postroute_obj',
              '-CFLAGS', '-DVERILATOR -DMAPPED -std=c++17' + module_spec['defines'], *postroute_sources,
              work / 'cells.v', SOURCE.parent / 'tests' / module_spec['harness'], '-MAKEFLAGS',
              'CXX=' + args.cxx + ' LINK=' + args.cxx + ' OPT_FAST=-O0 OPT_SLOW=-O0'],
              timeout=1800 if top in ('ReceiveFilter', 'PipelinedReceiveFilter') else 900)
        arguments = [] if module_spec['fixture'] is None else [work / module_spec['fixture']]
        stage('postroute-vectors', [work / 'postroute_obj' / ('V' + top), *arguments,
                                   work / 'postroute_trace.csv'], timeout=180)
        if digest(work / 'postroute_trace.csv') != digest(work / 'cpp_trace.csv'):
            raise ValueError('native and postroute module trajectories differ')
        report['native_postroute_exact'] = True
        report['artifact_sha256'] = {name: digest(work / name) for name in
                                     ('postroute.v', 'core.cdl', 'core.spef', 'core.def')}
        # A timing-failed candidate cannot be promoted. Keep its routed
        # evidence and return for correction before spending time on fill,
        # full DRC and LVS. Every passing candidate still requires all checks.
        report['timing'] = check_timing(work)
        failures = []
        report['stream'] = stream(pdk, work, top)
        stage('fill', [tools / 'bin/klayout', '-zz', '-r', COMMON / 'fill_core.py', '-rd',
                       'output_file=' + str(work / 'module_filled.gds'), work / 'repeater_core_unfilled.gds'],
              timeout=7200 if top in ('ReceiveFilter', 'PipelinedReceiveFilter') else 1800)
        try:
            stage('drc', [pdk_python, tech / 'drc/run_drc.py', '--path', work / 'module_filled.gds',
                          '--topcell', top, '--run_dir', work / 'drc', '--mp', str(workers),
                          '--density_thr', str(workers), '--antenna'],
                  timeout=7200 if top in ('ReceiveFilter', 'PipelinedReceiveFilter') else 3600, fatal=False)
            report['drc'] = check_drc(work / f'drc/module_filled_{top}_full.lyrdb', tech / 'drc')
        except Exception as error:
            failures.append('drc: ' + str(error))
        (work / 'lvs.cir').write_text(f'.include "{library / "cdl/sg13g2_stdcell.cdl"}"\n'
                                     f'.include "{work / "core.cdl"}"\n')
        try:
            stage('lvs', [pdk_python, tech / 'lvs/run_lvs.py', '--layout', work / 'module_filled.gds',
                          '--netlist', work / 'lvs.cir', '--topcell', top, '--run_mode', 'deep', '--run_dir', work / 'lvs'],
                  timeout=3600 if top in ('ReceiveFilter', 'PipelinedReceiveFilter') else 1800, fatal=False)
            report['lvs'] = check_lvs(work / 'lvs/module_filled.lvsdb', top, ports)
        except Exception as error:
            failures.append('lvs: ' + str(error))
        report['artifact_sha256']['module_filled.gds'] = digest(work / 'module_filled.gds')
        changed = changed_inputs(report['input_sha256'], report['retained_input_paths'])
        if changed:
            failures.append('inputs changed during physical implementation: ' + ', '.join(changed))
        if failures:
            report.update(status='failed', errors=failures)
        else:
            final = work / (top + '.gds')
            shutil.copyfile(work / 'module_filled.gds', final)
            report.update(status='component_checks_pass', gds=str(final))
        save()
        return 1 if failures else 0
    except BaseException as error:
        interrupted = isinstance(error, (KeyboardInterrupt, SystemExit))
        errors = list(report.get('errors', []))
        errors.extend(value for value in locals().get('failures', []) if value not in errors)
        errors.append(str(error) or type(error).__name__)
        report.update(status='aborted' if interrupted else 'failed', errors=errors)
        save()
        raise


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', choices=MODULES, default='ReceiveFilter')
    parser.add_argument('--mapped-work', type=Path, default=ANALOG / 'build/1G/tests/hardware/filter_datapath')
    parser.add_argument('--work', type=Path, default=SOURCE / 'results/receive_filter')
    parser.add_argument('--work-root', type=Path,
                        help='create a fresh retained run directory below this root (repeatable regression mode)')
    parser.add_argument('--pdk', type=Path, default=ANALOG / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ANALOG / 'out/repeater-toolchain')
    parser.add_argument('--verilator', default=shutil.which('verilator'))
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--workers', type=int, choices=(1, 2), default=1)
    parser.add_argument('--route-threads', type=int, choices=(1, 2), default=1,
                        help='detailed-route workers; one bounds memory on the large receive filter')
    parser.add_argument('--resume-placement', type=Path,
                        help='import matching saved placement into a fresh run; repeat every subsequent check')
    parser.add_argument('--resume-routed-database', type=Path,
                        help='recompute extraction, timing, equivalence, DRC and LVS from a routed OpenDB checkpoint')
    parser.add_argument('--max-transition', type=float, default=.5,
                        help='final slew limit in ns; sizing retains the tighter 0.3 ns target')
    args = parser.parse_args()
    if args.work_root is not None:
        args.work_root.mkdir(parents=True, exist_ok=True)
        args.work = Path(tempfile.mkdtemp(prefix=args.module + '-', dir=args.work_root.resolve()))
    print('Physical work: ' + str(args.work.resolve()), flush=True)
    raise SystemExit(implement(args))
