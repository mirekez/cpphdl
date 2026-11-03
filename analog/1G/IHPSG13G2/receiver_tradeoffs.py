#!/usr/bin/env python3
"""Evaluate explicit same-function receiver sizing trials with setup/hold guards."""
import argparse
import csv
import gc
import json
import os
from pathlib import Path
import re
import sys

from receiver_extracted_sizing import ROOT, HERE, digest, slack
from sizing_equivalence import compare, models
from buffer_equivalence import parse, BUFFERS
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def run(args):
    sized, routed, work = args.sized.resolve(), args.routed.resolve(), args.output.resolve()
    evidence = json.loads((sized / 'checks.json').read_text())
    if evidence.get('status') != 'diagnostic_complete':
        raise ValueError('completed extracted-sizing candidate required')
    hashes = dict(evidence['source_sha256'])
    hashes.update(evidence['artifact_sha256'])
    for name in ('core.spef', 'cells.v', 'physical_module.json'):
        path = routed / name
        if hashes.get(str(path)) != digest(path):
            raise ValueError('sizing candidate belongs to another routed implementation')
    cells, library = parse(sized / 'after.v')['cells'], models(routed / 'cells.v')
    trials = json.loads(args.trials.read_text())
    if not isinstance(trials, list) or not 1 <= len(trials) <= 200:
        raise ValueError('expected 1 to 200 explicit sizing trials')
    for trial in trials:
        if not isinstance(trial, list) or len(trial) != 2 or any(
                not isinstance(s, str) or not re.fullmatch(r'\w+', s) for s in trial):
            raise ValueError('expected simple instance/master names')
        name, target = trial
        old = cells[name][0]
        if (old not in BUFFERS or target not in BUFFERS or library[old] != library[target]):
            raise ValueError('only proven transparent buffer/delay substitutions are permitted')
    del cells, library, evidence
    gc.collect()
    work.mkdir(parents=True, exist_ok=False)
    script = work / 'run.tcl'
    script.write_text('''set work $::env(REPEATER_WORK)
set input $::env(RECEIVER_ROUTED_WORK)
set sized $::env(RECEIVER_SIZED_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$sized/sized.odb"
read_sdc "$sized/input.sdc"
set_propagated_clock [all_clocks]
write_guides "$work/restored.guide"
read_guides "$work/restored.guide"
foreach corner {slow fast} { read_spef -corner $corner "$input/core.spef" }
write_verilog "$work/before.v"
source "$::env(RECEIVER_SOURCE)/receiver_timing_tradeoffs.tcl"
''' + 'set trials {' + ' '.join('{' + ' '.join(t) + '}' for t in trials) + '}\n' + '''
receiver_timing_tradeoffs $trials "$work/trials.csv" 0.1
report_worst_slack -max -digits 9 > "$work/after_setup.rpt"
report_worst_slack -min -digits 9 > "$work/after_hold.rpt"
report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \\
    -fields {slew cap fanout input_pins} -digits 9 > "$work/after_paths.rpt"
report_check_types -max_slew -max_capacitance -max_fanout -violators > "$work/after_electrical.rpt"
write_verilog "$work/after.v"
write_db "$work/candidate.odb"
exit
''')
    for path in (Path(__file__).resolve(), HERE / 'receiver_timing_tradeoffs.tcl',
                 args.trials.resolve(), sized / 'checks.json', script):
        hashes[str(path)] = digest(path)
    recheck = work / 'recheck.tcl'
    recheck.write_text('''set work $::env(REPEATER_WORK)
set input $::env(RECEIVER_ROUTED_WORK)
set sized $::env(RECEIVER_SIZED_WORK)
set lib "$::env(REPEATER_PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lib"
set_thread_count 1
define_corners slow fast
read_liberty -corner slow "$lib/sg13g2_stdcell_slow_1p08V_125C.lib"
read_liberty -corner fast "$lib/sg13g2_stdcell_fast_1p32V_m40C.lib"
read_db "$work/candidate.odb"
read_sdc "$sized/input.sdc"
set_propagated_clock [all_clocks]
foreach corner {slow fast} { read_spef -corner $corner "$input/core.spef" }
report_worst_slack -max -digits 9 > "$work/recheck_setup.rpt"
report_worst_slack -min -digits 9 > "$work/recheck_hold.rpt"
exit
''')
    hashes[str(recheck)] = digest(recheck)
    report = dict(status='running', stage='waiting_for_memory_slot', source_sha256=hashes,
                  pid=os.getpid(), timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        tmp = work / 'checks.tmp'
        tmp.write_text(json.dumps(report, indent=2) + '\n')
        tmp.replace(work / 'checks.json')

    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('trial input changed: ' + name)

    save()
    try:
        with memory_slot(['receiver-timing-tradeoffs'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, RECEIVER_ROUTED_WORK=str(routed), RECEIVER_SIZED_WORK=str(sized),
                               REPEATER_WORK=str(work), REPEATER_PDK=str(args.pdk.resolve()),
                               RECEIVER_SOURCE=str(HERE), OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'timing_trials'
            save()
            with (work / 'trials.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                                  args.tools.resolve() / 'bin/openroad', '-no_init', '-exit', script],
                                 env=environment, stdout=log, stderr=-2, timeout=3600)
            report['returncode'] = result.returncode
            if result.returncode:
                raise ValueError('timing trials failed; see trials.log')
            report['stage'] = 'fresh_process_timing_check'
            save()
            with (work / 'recheck.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                                  args.tools.resolve() / 'bin/openroad', '-no_init', '-exit', recheck],
                                 env=environment, stdout=log, stderr=-2, timeout=600)
            report['recheck_returncode'] = result.returncode
            if result.returncode:
                raise ValueError('fresh process timing check failed')
            for kind, mode in [('setup', 'max'), ('hold', 'min')]:
                if abs(slack(work / ('after_' + kind + '.rpt'), mode) -
                       slack(work / ('recheck_' + kind + '.rpt'), mode)) > .001:
                    raise ValueError('fresh extracted-RC timing differs by more than 1 ps')
            proof = compare(work / 'before.v', work / 'after.v', routed / 'cells.v')
            (work / 'equivalence.json').write_text(json.dumps(proof, indent=2) + '\n')
            with (work / 'trials.csv').open() as stream:
                records = list(csv.DictReader(stream))
            verify()
            report.update(status='diagnostic_complete', stage='complete',
                          accepted_trials=sum(r['accepted'] == '1' for r in records),
                          resized_cells=proof['resized_cells'],
                          fresh_process_timing_agrees=True,
                          setup_ns=slack(work / 'after_setup.rpt', 'max'),
                          hold_ns=slack(work / 'after_hold.rpt', 'min'),
                          artifact_sha256={str(p): digest(p) for p in work.iterdir()
                                           if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sized', 'routed', 'trials', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
