#!/usr/bin/env python3
"""Iterate same-function cell sizing on reported receiver data paths.

Each round reloads the retained extracted RC in a fresh process. The final
independent reload and Boolean endpoint proof are mandatory. These fixed-wire
diagnostics do not qualify changed placement, routing or physical timing.
"""
import argparse
import gc
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from sizing_equivalence import compare, models
from buffer_equivalence import parse
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def slack(path, kind):
    values = re.findall(r'worst slack ' + kind + r'\s+([-+\deE.]+)', path.read_text())
    if len(values) != 1 or not math.isfinite(float(values[0])):
        raise ValueError('missing finite slack: ' + str(path))
    return float(values[0])


def data_trials(paths, cells, library, limit=200):
    if not isinstance(limit, int) or not 1 <= limit <= 200:
        raise ValueError('trial limit must be 1 to 200')
    result = []
    for path in paths.split('Startpoint:')[1:]:
        lines = path.splitlines()
        # Exclude launch and capture clock trees. Start at a real register Q
        # and stop before the capture-clock section of the timing report.
        start = next((i for i, line in enumerate(lines) if re.search(r'/Q(?:_N)? \(', line)), None)
        stop = next((i for i, line in enumerate(lines) if 'data arrival time' in line), None)
        if start is None or stop is None or stop <= start:
            continue
        for name, master in re.findall(r' (\w+)/\w+ \((sg13g2_\w+)\)', '\n'.join(lines[start:stop])):
            if name not in cells or cells[name][0] != master or master not in library:
                raise ValueError('timing path does not describe the seed netlist')
            family = master.rsplit('_', 1)[0]
            for target in sorted(library):
                if (target != master and target.rsplit('_', 1)[0] == family and
                        library[target] == library[master] and [name, target] not in result):
                    result.append([name, target])
                    if len(result) == limit:
                        return result
    if not result:
        raise ValueError('no proven same-function data-path sizing trials')
    return result


def script(trials=None):
    # Paths enter Tcl only through environment variables; names below have
    # been parsed as simple Verilog identifiers, never arbitrary Tcl text.
    text = '''set work $::env(REPEATER_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$::env(RECEIVER_SEED)"
if {[[ord::get_db_block] getName] != "PCSReceiver"} { error "receiver-only sizing" }
read_sdc "$::env(RECEIVER_SDC)"
set_propagated_clock [all_clocks]
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
foreach corner {slow fast} { read_spef -corner $corner "$::env(RECEIVER_SPEF)" }
report_worst_slack -max -digits 9 > "$work/before_setup.rpt"
report_worst_slack -min -digits 9 > "$work/before_hold.rpt"
'''
    if trials is not None:
        if any(not re.fullmatch(r'\w+', value) for trial in trials for value in trial):
            raise ValueError('unsafe instance or master name')
        text += '''write_verilog "$work/before.v"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
'''
        text += 'set trials {' + ' '.join('{' + ' '.join(t) + '}' for t in trials) + '}\n'
        text += '''receiver_timing_tradeoffs $trials "$work/trials.csv" 0.1
report_worst_slack -max -digits 9 > "$work/after_setup.rpt"
report_worst_slack -min -digits 9 > "$work/after_hold.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \\
    -fields {slew cap fanout input_pins} -digits 9 > "$work/after_paths.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/after_electrical.rpt"
write_verilog "$work/after.v"
write_db "$work/candidate.odb"
'''
    return text + 'exit\n'


def run(args):
    seed, routed, constraints, work = (p.resolve() for p in (args.seed, args.routed, args.constraints, args.output))
    proof = json.loads((seed / 'checks.json').read_text())
    if proof.get('status') != 'diagnostic_complete' or not proof.get('fresh_process_timing_agrees'):
        raise ValueError('seed requires completed independent extracted-RC timing check')
    if not 1 <= args.rounds <= 8:
        raise ValueError('round count must be 1 to 8')
    hashes = dict(proof['source_sha256'])
    for name, value in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != value:
            raise ValueError('conflicting seed provenance')
        hashes[name] = value
    library_root = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib'
    for p in (constraints, routed / 'core.spef', routed / 'cells.v',
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad',
              library_root / 'sg13g2_stdcell_slow_1p08V_125C.lib',
              library_root / 'sg13g2_stdcell_fast_1p32V_m40C.lib'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('seed belongs to another extracted design or constraint set')
    for p in (Path(__file__).resolve(), HERE / 'receiver_timing_tradeoffs.tcl', seed / 'checks.json'):
        hashes[str(p)] = digest(p)

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('sizing input changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', source_sha256=hashes,
                  rounds=[], timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-data-path-sizing'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, RECEIVER_SDC=str(constraints),
                RECEIVER_SPEF=str(routed / 'core.spef'), REPEATER_PDK=str(args.pdk.resolve()),
                RECEIVER_SOURCE=str(HERE), OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())

            def invoke(directory, database, trials):
                directory.mkdir()
                source = directory / 'run.tcl'
                source.write_text(script(trials))
                environment.update(REPEATER_WORK=str(directory), RECEIVER_SEED=str(database))
                with (directory / 'run.log').open('w') as log:
                    result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                        args.tools.resolve() / 'bin/openroad', '-no_init', '-exit', source],
                        env=environment, stdout=log, stderr=-2, timeout=1800)
                if result.returncode:
                    raise ValueError('sizing/recheck failed: ' + str(directory))
                verify()

            current = seed
            previous_setup, previous_hold = proof['setup_ns'], proof['hold_ns']
            for index in range(args.rounds):
                library = models(routed / 'cells.v')
                cells = parse(current / 'after.v')['cells']
                trials = data_trials((current / 'after_paths.rpt').read_text(), cells, library)
                del library, cells
                gc.collect()
                following = work / ('round-' + str(index + 1))
                report['stage'] = 'sizing_round_' + str(index + 1)
                save()
                invoke(following, current / 'candidate.odb', trials)
                for kind, mode, expected in [('setup', 'max', previous_setup), ('hold', 'min', previous_hold)]:
                    if abs(slack(following / ('before_' + kind + '.rpt'), mode) - expected) > .001:
                        raise ValueError('fresh receiver timing differs by more than 1 ps')
                previous_setup = slack(following / 'after_setup.rpt', 'max')
                previous_hold = slack(following / 'after_hold.rpt', 'min')
                if previous_hold < .1:
                    raise ValueError('candidate lost required hold margin')
                report['rounds'].append(dict(trials=len(trials), setup_ns=previous_setup, hold_ns=previous_hold))
                current = following
                save()
                if previous_setup >= .1:
                    break
            report['stage'] = 'independent_recheck'
            save()
            recheck = work / 'recheck'
            invoke(recheck, current / 'candidate.odb', None)
            for kind, mode, expected in [('setup', 'max', previous_setup), ('hold', 'min', previous_hold)]:
                if abs(slack(recheck / ('before_' + kind + '.rpt'), mode) - expected) > .001:
                    raise ValueError('final fresh timing differs by more than 1 ps')
            equivalence = compare(seed / 'after.v', current / 'after.v', routed / 'cells.v')
            (work / 'equivalence.json').write_text(json.dumps(equivalence, indent=2) + '\n')
            verify()
            report.update(status='diagnostic_complete', stage='complete', candidate=str(current),
                fresh_process_timing_agrees=True, setup_ns=previous_setup, hold_ns=previous_hold,
                resized_cells=equivalence['resized_cells'], artifact_sha256={str(p): digest(p)
                    for p in work.rglob('*') if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'routed', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
