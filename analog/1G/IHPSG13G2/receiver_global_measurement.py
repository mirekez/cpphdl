#!/usr/bin/env python3
"""Measure nominal / 1.75x wire-C / restored nominal RC on a receiver diagnostic.

This is a global-route optimization screen. Detailed routing, extracted timing,
functional replay and layout checks remain mandatory regardless of its result.
"""
import argparse
import json
import math
import os
from pathlib import Path
import time

from buffer_equivalence import compare
from receiver_output_stage import digest
from receiver_path_sizing import HERE, ROOT, slack
from receiver_route_probe import screen
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def check_restoration(proof, estimates):
    if set(estimates) != {'before', 'margin', 'restored'}:
        raise ValueError('all three RC measurements are required')
    for row in estimates.values():
        if not isinstance(row.get('electrical_pass'), bool):
            raise ValueError('electrical result must be a boolean')
        for kind in ('setup_ns', 'hold_ns'):
            value = row.get(kind)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError('finite setup/hold measurements required')
    for name in ('before', 'restored'):
        for kind in ('setup_ns', 'hold_ns'):
            if abs(estimates[name][kind] - proof['rounds'][-1][kind]) > .001:
                raise ValueError('nominal reload/restoration differs by more than 1 ps')
    return all(row['setup_ns'] >= .1 and row['hold_ns'] >= .1 and row['electrical_pass']
               for row in estimates.values())


def run(args):
    origin, work = args.origin.resolve(), args.output.resolve()
    proof = json.loads((origin / 'checks.json').read_text())
    screen(proof)
    hashes = {}
    for entries in (proof['source_sha256'], proof['artifact_sha256']):
        for name, expected in entries.items():
            if name in hashes and hashes[name] != expected:
                raise ValueError('conflicting global diagnostic provenance')
            hashes[name] = expected
    for name in ('candidate.odb', 'candidate.sdc', 'after.v'):
        path = origin / name
        if proof['artifact_sha256'].get(str(path)) != digest(path):
            raise ValueError('changed or unbound diagnostic artifact: ' + name)
    tools, pdk = args.tools.resolve(), args.pdk.resolve()
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    for path in [tools / 'bin/openroad', tools / 'root/usr/bin/openroad'] + [
            library / ('lib/sg13g2_stdcell_' + corner + '.lib')
            for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]:
        if hashes.get(str(path)) != digest(path):
            raise ValueError('measurement tool/library differs from producer: ' + str(path))
    for path in (origin / 'checks.json', Path(__file__).resolve(),
                 HERE / 'receiver_output_stage.py', HERE / 'receiver_route_probe.py',
                 HERE / 'receiver_path_sizing.py', HERE / 'buffer_equivalence.py',
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
                 ROOT / 'pdk/1G_repeater_IHPSG13G2/routing_rc_margin.tcl',
                 library / 'lef/sg13g2_tech.lef'):
        if str(path) in hashes and hashes[str(path)] != digest(path):
            raise ValueError('measurement source changed: ' + str(path))
        hashes[str(path)] = digest(path)

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('global measurement input changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    script = work / 'run.tcl'
    script.write_text(f'''set_thread_count 1
define_corners slow fast
read_liberty -corner slow {{{library}/lib/sg13g2_stdcell_slow_1p08V_125C.lib}}
read_liberty -corner fast {{{library}/lib/sg13g2_stdcell_fast_1p32V_m40C.lib}}
read_db {{{origin}/candidate.odb}}
if {{[[ord::get_db_block] getName] != "PCSReceiver"}} {{ error "receiver-only RC measurement" }}
read_sdc {{{origin}/candidate.sdc}}
set_propagated_clock [all_clocks]
set_max_transition 0.5 [current_design]
set_max_capacitance 0.1 [current_design]
set_max_fanout 8 [current_design]
source {{{ROOT}/pdk/1G_repeater_IHPSG13G2/routing_rc_margin.tcl}}
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
set_global_routing_layer_adjustment Metal2-Metal5 0.30
check_placement -verbose
foreach name {{before margin restored}} factor {{1 1.75 1}} {{
    apply_routing_capacitance_margin $factor {{{library}/lef/sg13g2_tech.lef}}
    global_route
    estimate_parasitics -placement
    estimate_parasitics -global_routing
    report_worst_slack -max -digits 9 > "{work}/${{name}}_setup.rpt"
    report_worst_slack -min -digits 9 > "{work}/${{name}}_hold.rpt"
    report_check_types -max_slew -max_capacitance -max_fanout -violators > "{work}/${{name}}_electrical.rpt"
    report_checks -path_delay max -group_path_count 50 -format full_clock_expanded \\
        -fields {{slew cap fanout input_pins}} -digits 9 > "{work}/${{name}}_paths.rpt"
}}
write_verilog {{{work}/reloaded.v}}
puts "RECEIVER_MARGIN_SCREEN_DONE"
exit
''')
    hashes[str(script)] = digest(script)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(),
                  input_sha256=hashes, timing_qualified=False, physical_qualified=False,
                  full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-global-measurement'], exclusive=True) as owned:
            verify()
            report['stage'] = 'measure'
            save()
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            started = time.monotonic()
            with (work / 'run.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    tools / 'bin/openroad', '-no_init', '-exit', script],
                    env=environment, stdout=log, stderr=-2, timeout=600)
            report['native_seconds'] = time.monotonic() - started
            report['native_returncode'] = result.returncode
            log = (work / 'run.log').read_text()
            if result.returncode or log.count('RECEIVER_MARGIN_SCREEN_DONE') != 1:
                raise ValueError('native RC measurement failed')
            if any(s in log for s in ('EST-0026', 'EST-0027', 'GRT-0304', 'Missing route to pin')):
                raise ValueError('incomplete routes during RC measurement')
            report['connectivity'] = compare(origin / 'after.v', work / 'reloaded.v')
            if report['connectivity']['removed_buffers'] or report['connectivity']['removed_fillers']:
                raise ValueError('RC measurement changed cells or endpoints')
            report['estimates'] = {name: dict(
                setup_ns=slack(work / (name + '_setup.rpt'), 'max'),
                hold_ns=slack(work / (name + '_hold.rpt'), 'min'),
                electrical_pass='VIOLATED' not in (work / (name + '_electrical.rpt')).read_text().upper())
                for name in ('before', 'margin', 'restored')}
            report['optimization_screen_pass'] = check_restoration(proof, report['estimates'])
            verify()
            report.update(status='global_margin_measurement_complete', stage='complete')
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
    for name in ('origin', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
