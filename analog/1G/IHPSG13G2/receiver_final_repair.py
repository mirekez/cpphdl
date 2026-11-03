#!/usr/bin/env python3
"""Repair receiver electrical loading after sizing, then screen both RC estimates.

The output is a global-route candidate; detailed routing and new RC extraction
remain mandatory. Never qualify timing using the old SPEF after buffer insertion.
"""
import argparse
import gc
import json
import math
import os
from pathlib import Path
import sys
import time

from receiver_path_sizing import HERE, ROOT, digest, slack
from receiver_margin_route import margin_screen
from buffer_equivalence import compare as buffer_compare
from sizing_equivalence import compare as sizing_compare
from repair_equivalence import compare as repair_compare
from rebuffer_equivalence import compare as rebuffer_compare
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def final_screen(work):
    require_complete_routes((work / 'global.log').read_text())
    values = margin_screen(work)
    if 'VIOLATED' in (work / 'margin_electrical.rpt').read_text().upper():
        raise ValueError('post-sizing enlarged-C electrical limits fail')
    return values


def repair_margin(value):
    value_number = float(value)
    if isinstance(value, bool) or not math.isfinite(value_number) or not 0 <= value_number <= 40:
        raise ValueError('extra electrical repair margin must be between 0 and 40 percent')
    return value_number


def require_complete_routes(log):
    begin, end = 'RECEIVER_FINAL_ROUTE_CHECK_BEGIN', 'RECEIVER_FINAL_ROUTE_CHECK_END'
    if log.count(begin) != 1 or log.count(end) != 1 or log.index(begin) >= log.index(end):
        raise ValueError('missing complete final route check')
    final = log.split(begin, 1)[1].split(end, 1)[0]
    if any(message in final for message in ('EST-0026', 'EST-0027', 'GRT-0304', 'Missing route to pin')):
        raise ValueError('receiver parasitic estimate has an unrouted pin')


def run(args):
    slew_margin, cap_margin = repair_margin(args.slew_margin), repair_margin(args.cap_margin)
    if not 1 <= args.trial_limit <= 200:
        raise ValueError('trial limit must be 1 to 200')
    seed, routed, work = (p.resolve() for p in (args.seed, args.routed, args.output))
    proof = json.loads((seed / 'checks.json').read_text())
    if proof.get('status') != 'diagnostic_complete' or not proof.get('fresh_process_timing_agrees'):
        raise ValueError('completed independently reloaded sizing seed required')
    candidate = Path(proof['candidate']).resolve()
    if not candidate.is_relative_to(seed):
        raise ValueError('candidate lies outside the verified sizing run')
    hashes = dict(proof['source_sha256'])
    for name, expected in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != expected:
            raise ValueError('conflicting sizing provenance')
        hashes[name] = expected
    for p in (candidate / 'candidate.odb', candidate / 'after.v', routed / 'core.spef',
              routed / 'cells.v', args.constraints.resolve(),
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('electrical repair input differs from the sizing proof: ' + str(p))
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = args.pdk.resolve() / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('electrical repair timing library changed')
    for p in (Path(__file__).resolve(), HERE / 'receiver_final_repair.tcl',
              HERE / 'repair_equivalence.py', HERE / 'receiver_margin_route.py',
              HERE / 'rebuffer_equivalence.py',
              HERE / 'receiver_local_unbuffer.tcl',
              HERE / 'receiver_data_trials.py',
              HERE / 'receiver_clock_path_trials.tcl', HERE / 'receiver_timing_tradeoffs.tcl',
              ROOT / 'pdk/1G_repeater_IHPSG13G2/finish_timing.tcl',
              ROOT / 'pdk/1G_repeater_IHPSG13G2/routing_rc_margin.tcl', seed / 'checks.json'):
        hashes[str(p)] = digest(p)

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('electrical repair source changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  source_sha256=hashes, wire_capacitance_optimization_factor=1.75,
                  extra_slew_margin_percent=slew_margin, extra_cap_margin_percent=cap_margin,
                  buffer_replacement_enabled=args.rebuffer,
                  data_drive_trials_enabled=args.data_sizing,
                  trials_per_pass=args.trial_limit, trial_rc_model='fresh_global_routing',
                  timing_qualified=False, physical_qualified=False,
                  full_chip_qualified=False)
    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')
    save()
    try:
        with memory_slot(['receiver-final-repair'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work),
                REPEATER_PDK=str(args.pdk.resolve()), RECEIVER_SEED=str(candidate / 'candidate.odb'),
                RECEIVER_SPEF=str(routed / 'core.spef'), RECEIVER_SDC=str(args.constraints.resolve()),
                RECEIVER_SOURCE=str(HERE), REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                RECEIVER_SLEW_MARGIN=str(slew_margin), RECEIVER_CAP_MARGIN=str(cap_margin),
                RECEIVER_REBUFFER='1' if args.rebuffer else '0',
                RECEIVER_DATA_SIZING='1' if args.data_sizing else '0',
                RECEIVER_TRIAL_LIMIT=str(args.trial_limit),
                RECEIVER_PYTHON=sys.executable, RECEIVER_CELLS=str(routed / 'cells.v'),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'global'
            save()
            started = time.monotonic()
            with (work / 'global.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    args.tools.resolve() / 'bin/openroad', '-no_init', '-exit',
                    HERE / 'receiver_final_repair.tcl'], env=environment,
                    stdout=log, stderr=-2, timeout=7200)
            report['stages'].append(dict(name='global', returncode=result.returncode,
                                         seconds=time.monotonic() - started))
            if result.returncode:
                raise ValueError('electrical repair/global routing failed; see global.log')
            require_complete_routes((work / 'global.log').read_text())
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) - proof[kind + '_ns']) > .001:
                    raise ValueError('electrical repair input timing differs by more than 1 ps')
            report['seed_connectivity'] = sizing_compare(candidate / 'after.v', work / 'before.v', routed / 'cells.v')
            report['filler_connectivity'] = buffer_compare(work / 'before.v', work / 'unfilled.v', defer_fillers=True)
            compare_repair = rebuffer_compare if args.rebuffer else repair_compare
            report['buffer_connectivity'] = compare_repair(work / 'unfilled.v', work / 'repaired.v', routed / 'cells.v')
            gc.collect()
            report['placement_connectivity'] = buffer_compare(work / 'repaired.v', work / 'global.v')
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                            hold_ns=slack(work / 'global_hold.rpt', 'min'))
            report['global_electrical_pass'] = 'VIOLATED' not in (work / 'global_electrical.rpt').read_text().upper()
            verify()
            report['margin_screen'] = final_screen(work)
            report.update(status='global_repair_candidate', stage='complete',
                artifact_sha256={str(p): digest(p) for p in work.iterdir()
                                 if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
                                     if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'routed', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--rebuffer', action='store_true',
                        help='allow proven identity-buffer replacement with setup and hold repair')
    parser.add_argument('--data-sizing', action='store_true',
                        help='try both smaller and larger equivalent data-path cells after electrical repair')
    parser.add_argument('--trial-limit', type=int, default=12,
                        help='maximum drive trials per pass (1 to 200); each rebuilds global RC')
    parser.add_argument('--slew-margin', type=repair_margin, default=0,
                        help='extra optimization margin; the 0.50 ns limit and 1.75x wire-C probe remain')
    parser.add_argument('--cap-margin', type=repair_margin, default=0,
                        help='extra optimization margin; the 0.10 pF limit and 1.75x wire-C probe remain')
    run(parser.parse_args())
