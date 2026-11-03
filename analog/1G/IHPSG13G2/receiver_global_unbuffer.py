#!/usr/bin/env python3
"""Measure bounded data-buffer removal using fresh global routes and exact endpoints."""
import argparse
import json
import math
import os
from pathlib import Path
import sys

from receiver_path_sizing import HERE, ROOT, digest, slack
from buffer_equivalence import compare
from rebuffer_equivalence import compare as compare_rebuffer

sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    trim_hold = getattr(args, 'trim_hold', False)
    if not math.isfinite(args.span_um) or not 0 < args.span_um <= 200:
        raise ValueError('diagnostic neighbor span must be in (0, 200] um')
    if not (0 if trim_hold else 1) <= args.rounds <= 16:
        raise ValueError('diagnostic rounds must be in [1, 16], or zero with hold trimming')
    if not math.isfinite(args.target_slack) or not 0 <= args.target_slack <= 2:
        raise ValueError('diagnostic setup target must be in [0, 2] ns')
    if trim_hold and args.target_slack <= 0:
        raise ValueError('hold trimming requires a positive setup target')
    buffer_sinks = getattr(args, 'buffer_sinks', 2)
    if not isinstance(buffer_sinks, int) or not 1 <= buffer_sinks <= 4:
        raise ValueError('buffer sink bound must be an integer from 1 to 4')
    hold_trials = getattr(args, 'hold_trials', 18)
    hold_seconds = getattr(args, 'hold_seconds', 600)
    if not 1 <= hold_trials <= 200 or not 1 <= hold_seconds <= 900:
        raise ValueError('hold trials must be in [1, 200] and seconds in [1, 900]')
    seed, work = args.seed.resolve(), args.output.resolve()
    if (seed / 'physical_module.json').exists():
        metadata = json.loads((seed / 'physical_module.json').read_text())
        if metadata.get('top') != 'PCSReceiver':
            raise ValueError('receiver physical checkpoint required')
        database, constraints, prefix = 'finished_global.odb', 'finished_global.sdc', 'global'
        paths = [seed / 'physical_module.json']
        cells = seed / 'cells.v'
        seed_netlist = None
    else:
        metadata = json.loads((seed / 'checks.json').read_text())
        if (metadata.get('status') != 'diagnostic_complete' or not metadata.get('rounds') or
                metadata['rounds'][-1].get('name') != 'after' or
                metadata['rounds'][-1].get('connectivity', {}).get('status') != 'pass'):
            raise ValueError('completed endpoint-checked unbuffer diagnostic required')
        database, constraints, prefix = 'candidate.odb', 'candidate.sdc', 'after'
        paths = [seed / 'checks.json', seed / 'after.v']
        seed_netlist = seed / 'after.v'
        cells = Path(metadata['cell_models'])
        for name in (database, constraints, 'after.v', 'after_setup.rpt', 'after_hold.rpt'):
            if metadata['artifact_sha256'].get(str(seed / name)) != digest(seed / name):
                raise ValueError('diagnostic checkpoint changed: ' + name)
    paths += [seed / name for name in (database, constraints, prefix + '_setup.rpt', prefix + '_hold.rpt')]
    paths += [cells]
    paths += [Path(__file__).resolve(), HERE / 'receiver_global_unbuffer.tcl',
              HERE / 'receiver_local_unbuffer.tcl', HERE / 'buffer_equivalence.py',
              HERE / 'rebuffer_equivalence.py', HERE / 'sizing_equivalence.py',
              ROOT / 'pdk/1G_repeater_IHPSG13G2/finish_timing.tcl',
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad']
    if trim_hold:
        paths.append(HERE / 'receiver_hold_trim.tcl')
    paths += [args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' /
              f'sg13g2_stdcell_{corner}.lib' for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
    hashes = {str(p): digest(p) for p in paths}
    if seed_netlist:
        for path in [cells, args.tools.resolve() / 'bin/openroad',
                     args.tools.resolve() / 'root/usr/bin/openroad'] + paths[-2:]:
            if metadata['source_sha256'].get(str(path)) != hashes[str(path)]:
                raise ValueError('diagnostic tool/library input changed: ' + str(path))
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', source_sha256=hashes, pid=os.getpid(), maximum_span_um=args.span_um,
                  seed=str(seed), cell_models=str(cells), rounds_limit=args.rounds, buffer_sinks_limit=buffer_sinks,
                  setup_target_ns=args.target_slack, electrical_repair=args.electrical_repair,
                  trim_hold=trim_hold, hold_reserve_ns=0.1,
                  hold_trials_limit=hold_trials, hold_seconds_limit=hold_seconds,
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('changed unbuffer input: ' + name)

    save()
    try:
        with memory_slot(['receiver-global-unbuffer'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work),
                REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SOURCE=str(HERE),
                REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                RECEIVER_SEED=str(seed / database), RECEIVER_SDC=str(seed / constraints),
                RECEIVER_UNBUFFER_ROUNDS=str(args.rounds), RECEIVER_UNBUFFER_TARGET=str(args.target_slack),
                RECEIVER_UNBUFFER_REPAIR='1' if args.electrical_repair else '0',
                RECEIVER_UNBUFFER_SINKS=str(buffer_sinks),
                RECEIVER_TRIM_HOLD='1' if trim_hold else '0',
                RECEIVER_HOLD_TRIALS=str(hold_trials), RECEIVER_HOLD_SECONDS=str(hold_seconds),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            environment['RECEIVER_UNBUFFER_SPAN'] = str(args.span_um)
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            with (work / 'run.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    args.tools.resolve() / 'bin/openroad', '-no_init', '-exit',
                    HERE / 'receiver_global_unbuffer.tcl'], env=environment,
                    stdout=log, stderr=-2, timeout=1800)
            if result.returncode:
                raise ValueError('unbuffer diagnostic failed; inspect run.log')
            log_text = (work / 'run.log').read_text()
            if any(marker in log_text for marker in ('EST-0026', 'EST-0027', 'GRT-0304', 'Missing route to pin')):
                raise ValueError('unbuffer diagnostic has incomplete route estimates')
            if seed_netlist:
                report['seed_connectivity'] = compare(seed_netlist, work / 'before.v')
                if report['seed_connectivity']['removed_buffers']:
                    raise ValueError('reloaded checkpoint differs from its saved netlist')
            report['rounds'] = []
            for name in (['before'] + [f'round_{i}' for i in range(1, args.rounds + 1)] +
                         (['hold'] if trim_hold else []) + ['after']):
                if not (work / (name + '.v')).exists():
                    continue
                row = dict(name=name, setup_ns=slack(work / (name + '_setup.rpt'), 'max'),
                           hold_ns=slack(work / (name + '_hold.rpt'), 'min'),
                           electrical_pass='VIOLATED' not in (work / (name + '_electrical.rpt')).read_text())
                if name != 'before':
                    row['connectivity'] = (compare_rebuffer(work / 'before.v', work / (name + '.v'), cells)
                                           if args.electrical_repair or trim_hold else
                                           compare(work / 'before.v', work / (name + '.v')))
                report['rounds'].append(row)
            report['reloaded_setup_difference_ns'] = report['rounds'][0]['setup_ns'] - slack(seed / (prefix + '_setup.rpt'), 'max')
            report['reloaded_hold_difference_ns'] = report['rounds'][0]['hold_ns'] - slack(seed / (prefix + '_hold.rpt'), 'min')
            if max(abs(report['reloaded_setup_difference_ns']), abs(report['reloaded_hold_difference_ns'])) > .001:
                raise ValueError('initial global-route reload differs by more than 1 ps')
            verify()
            report['status'] = 'diagnostic_complete'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
                                    if p.is_file() and p.name != 'checks.json'}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--span-um', type=float, default=60,
                        help='explicit neighbor-span bound for diagnostic removal (maximum 200 um)')
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--target-slack', type=float, default=.35)
    parser.add_argument('--electrical-repair', action='store_true', help='repair actual electrical violations after removal')
    parser.add_argument('--trim-hold', action='store_true',
                        help='reduce excess data-path delay cells while preserving 0.1 ns hold reserve')
    parser.add_argument('--buffer-sinks', type=int, choices=range(1, 5), default=2,
                        help='maximum sinks on an explicitly selected data buffer (default 2)')
    parser.add_argument('--hold-trials', type=int, default=18,
                        help='maximum delay-cell trials across all passes (1..200)')
    parser.add_argument('--hold-seconds', type=int, default=600,
                        help='stop starting hold trials after this many seconds (1..900)')
    run(parser.parse_args())
