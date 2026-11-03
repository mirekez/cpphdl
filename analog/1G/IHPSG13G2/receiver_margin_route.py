#!/usr/bin/env python3
"""Size a receiver with wire-capacitance margin before routing/extraction.

The 1.5x wire-C screen is an optimization target, not a characterized corner.
Final acceptance uses fresh nominal OpenRCX and the original timing limits.
"""
import argparse
import gc
import json
import math
import os
from pathlib import Path
import shutil
import sys
import time

from receiver_path_sizing import HERE, ROOT, digest, slack
from sizing_equivalence import compare as sizing_compare
from buffer_equivalence import compare as buffer_compare
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from checks import check_timing
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def nonnegative_setup_target(value):
    number = float(value)
    if isinstance(value, bool) or not math.isfinite(number) or number < 0:
        raise ValueError('optimization setup target must be finite and nonnegative')
    return number


def margin_screen(work, minimum_setup=.1):
    minimum_setup = nonnegative_setup_target(minimum_setup)
    values = {kind: slack(work / (prefix + '_' + kind + '.rpt'), mode)
              for prefix in ('margin',) for kind, mode in (('setup', 'max'), ('hold', 'min'))}
    nominal = {kind: slack(work / ('global_' + kind + '.rpt'), mode)
               for kind, mode in (('setup', 'max'), ('hold', 'min'))}
    if (min(values['setup'], nominal['setup']) < minimum_setup
            or min(values['hold'], nominal['hold']) < .1):
        raise ValueError(f'candidate misses {minimum_setup} ns setup target or 0.1 ns margin for hold')
    if 'VIOLATED' in (work / 'global_electrical.rpt').read_text().upper():
        raise ValueError('nominal global electrical limits fail')
    return dict(optimization=values, nominal=nominal)


def run(args):
    minimum_setup = nonnegative_setup_target(args.minimum_optimization_setup)
    if not 1 <= args.trial_limit <= 200:
        raise ValueError('trial limit must be 1 to 200')
    origin, work = args.origin.resolve(), args.output.resolve()
    proof = json.loads((origin / 'checks.json').read_text())
    if proof.get('status') != 'global_repair_candidate' or proof.get('stage') != 'complete':
        raise ValueError('completed electrical repair candidate required')
    for name in ('seed_connectivity', 'filler_connectivity', 'buffer_connectivity', 'placement_connectivity'):
        if proof.get(name, {}).get('status') != 'pass':
            raise ValueError('missing electrical-repair endpoint proof')
    hashes = dict(proof['source_sha256'])
    for name, value in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != value:
            raise ValueError('conflicting electrical-repair provenance')
        hashes[name] = value
    for p in (origin / 'global.odb', origin / 'global.v', args.constraints.resolve(), args.cells.resolve(),
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('margin sizing input differs from the verified candidate: ' + str(p))
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = args.pdk.resolve() / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('margin sizing timing library differs')
    for p in (Path(__file__).resolve(), HERE / 'receiver_margin_sizing.tcl',
              HERE / 'receiver_clock_path_trials.tcl', HERE / 'receiver_timing_tradeoffs.tcl',
              origin / 'checks.json'):
        hashes[str(p)] = digest(p)
    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('margin routing input changed: ' + name)
    verify()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
        source_sha256=hashes, wire_capacitance_optimization_factor=1.5,
        minimum_optimization_setup_ns=minimum_setup, minimum_optimization_hold_ns=.1,
        trials_per_pass=args.trial_limit, trial_rc_model='fresh_global_routing',
        timing_qualified=False, physical_qualified=False, full_chip_qualified=False)
    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')
    save()
    try:
        with memory_slot(['receiver-margin-route'], exclusive=True) as owned:
            environment = dict(os.environ, REPEATER_WORK=str(work),
                REPEATER_PDK=str(args.pdk.resolve()), REPEATER_TOOLS=str(args.tools.resolve()),
                REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                RECEIVER_SOURCE=str(HERE),
                RECEIVER_TRIAL_LIMIT=str(args.trial_limit),
                RECEIVER_SEED=str(origin / 'global.odb'), RECEIVER_SDC=str(args.constraints.resolve()),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            def stage(name, script, timeout, gib):
                report['stage'] = name
                save()
                environment['RECEIVER_ECO_STAGE'] = name
                started = time.monotonic()
                with (work / (name + '.log')).open('w') as log:
                    result = execute(['prlimit', '--as=' + str(gib * 1024**3), '--core=0', '--',
                        args.tools.resolve() / 'bin/openroad', '-no_init', '-exit', script],
                        env=environment, stdout=log, stderr=-2, timeout=timeout)
                report['stages'].append(dict(name=name, returncode=result.returncode,
                                            seconds=time.monotonic() - started))
                if result.returncode:
                    raise ValueError('margin route stage failed: ' + name)
                verify()
                save()
            stage('size', HERE / 'receiver_margin_sizing.tcl', 3600, 4)
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) - proof['global_estimates'][kind + '_ns']) > .001:
                    raise ValueError('margin sizing baseline differs by more than 1 ps')
            report['seed_connectivity'] = buffer_compare(origin / 'global.v', work / 'before.v')
            report['sizing_connectivity'] = sizing_compare(work / 'before.v', work / 'sized.v', args.cells)
            report['sized_placement_connectivity'] = buffer_compare(work / 'sized.v', work / 'clock_before.v')
            report['clock_connectivity'] = sizing_compare(work / 'clock_before.v', work / 'clock_after.v', args.cells)
            report['placement_connectivity'] = buffer_compare(work / 'clock_after.v', work / 'global.v')
            try:
                report['margin_screen'] = margin_screen(work, minimum_setup)
            except ValueError as error:
                report.update(status='global_margin_fail', error=str(error), stage='complete')
                return
            if shutil.disk_usage(work).free < 1000 * 1024**2:
                raise ValueError('detailed routing requires at least 1000 MiB free')
            gc.collect()
            stage('detail', HERE / 'receiver_local_route.tcl', 18000, 6)
            if (work / 'route_drc.rpt').read_text().strip():
                raise ValueError('detailed-router violations remain')
            stage('extract', HERE / 'receiver_local_route.tcl', 1800, 4)
            report['routed_connectivity'] = buffer_compare(work / 'postroute.v', work / 'global.v', defer_fillers=True)
            report.update(setup_ns=slack(work / 'setup.rpt', 'max'), hold_ns=slack(work / 'hold.rpt', 'min'))
            try:
                report['timing'] = check_timing(work)
                report['status'] = 'routed_timing_candidate_pass'
            except ValueError as error:
                report.update(status='routed_timing_fail', timing_error=str(error))
            report['stage'] = 'complete'
            verify()
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
    parser.add_argument('--trial-limit', type=int, default=12,
                        help='maximum clock-drive trials (1 to 200); each rebuilds global RC')
    for name in ('origin', 'constraints', 'cells', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--minimum-optimization-setup', type=nonnegative_setup_target, default=.1,
                        help='setup target for nominal and enlarged-C estimates; final extracted checks are unchanged')
    run(parser.parse_args())
