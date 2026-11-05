#!/usr/bin/env python3
"""Place the complete digital cell population together, without module boxes."""
import argparse
import json
import os
from pathlib import Path
import shutil
import time
from route_tech import prepare
from tool_process import configure_cleanup, execute
from unified_synthesis import digest

SOURCE = Path(__file__).resolve().parent


def constraints():
    # Initial placement budgets. Electrical IO arrival times and CDC constraints
    # must be derived/checked against the connected analog circuit before signoff.
    text = '# Placement budgets only; not extracted clock/CDC signoff.\n'
    for clock, period in [('ref_clk', 8), ('adc_clk', 4), ('raw_clk', 1000 / 136),
                          ('pcs_clk', 1000 / 136), ('tx_a_clk', 8), ('tx_b_clk', 8)]:
        text += f'create_clock -name {clock} -period {period:.12g} [get_ports {clock}]\n'
    text += '''set_clock_uncertainty 0.1 [all_clocks]
# Related physical clock sources still require measured insertion delays.
# These groups only isolate independent sources for initial placement.
set_clock_groups -asynchronous -group {ref_clk adc_clk} -group {raw_clk pcs_clk} -group tx_a_clk -group tx_b_clk
set_input_transition 0.1 [all_inputs]
set_input_delay 0.4 -clock ref_clk [get_ports {enable_in power_good_in restart_in* signal_ready_in* tx_clock_ready_in* pulse_in* configuration_in* negotiation_seed_in* scrambler_seed_in* address_in* reset}]
set_input_delay 0.4 -clock adc_clk [get_ports {comparisons_in* offset_in* gain_in* local_history_in*}]
set_output_delay 0.4 -clock adc_clk [get_ports {trial_out* acquire_out* convert_out* clear_out* evaluate_enable_out* aperture_out*}]
set_output_delay 0.4 -clock ref_clk [get_ports {processing_coarse_n_out* processing_fine_n_out* processing_clear_out processing_admit_out adc_ready_out processing_ready_out active_out fault_out pulse_out* pulse_enable_out* master_out* link_out*}]
set_output_delay 0.4 -clock pcs_clk [get_ports {recovered_period_out* recovered_tag_out* recovered_valid_out*}]
set_load 0.005 [all_outputs]
set_load 0.056 [get_ports {trial_out* acquire_out* main_p_out* main_n_out* history_p_out* history_n_out*}]
'''
    # sv2v packs ascending unpacked arrays with element zero at the high end.
    # Main/history output bits 31:16 belong to port A, 15:0 to port B.
    for name, width in [('main_p_out', 32), ('main_n_out', 32), ('history_p_out', 32), ('history_n_out', 32), ('symbols_out', 24)]:
        for i in range(width):
            clock = 'tx_a_clk' if i >= width // 2 else 'tx_b_clk'
            text += f'set_output_delay 0.4 -clock {clock} [get_ports {{{name}[{i}]}}]\n'
    return text


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'mapped_unplaced' or proof.get('fixed_logic_modules') != 0:
        raise ValueError('audited mapped digital leaf inventory required')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256']}
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('mapped input is stale')
    work, pdk, tools = args.work.resolve(), args.pdk.resolve(), args.tools.resolve()
    resume = None
    if args.resume_floorplan:
        resume = json.loads(args.resume_floorplan.read_text())
        if resume.get('status') != 'floorplanned_unplaced':
            raise ValueError('audited floorplan checkpoint required')
        dependencies = {**resume['source_sha256'], **resume['artifact_sha256']}
        if any(digest(p) != h for p, h in dependencies.items()):
            raise ValueError('floorplan checkpoint is stale')
        if resume['inventory']['movable_standard_cells'] != proof['movable_standard_cells']:
            raise ValueError('floorplan does not match mapped inventory')
        hashes.update(dependencies)
        hashes[str(args.resume_floorplan.resolve())] = digest(args.resume_floorplan)
    work.mkdir(parents=True, exist_ok=False)
    for path in [Path(__file__), SOURCE / 'unified_place.tcl', SOURCE / 'unified_synthesis.py',
                 SOURCE / 'route_tech.py', SOURCE / 'tool_process.py', tools / 'bin/openroad', args.proof.resolve()]:
        hashes[str(path)] = digest(path)
    refs = pdk / 'ihp-sg13g2/libs.ref'
    for family in ('sg13g2_stdcell', 'sg13g2_sram'):
        for pattern in ('lef/*.lef', 'lib/*.lib'):
            for path in (refs / family).glob(pattern):
                hashes[str(path)] = digest(path)
    if resume:
        previous = args.resume_floorplan.resolve().parent
        for name in ('route_tech.lef', 'constraints.sdc', 'inventory.json'):
            shutil.copy2(previous / name, work / name)
    else:
        prepare(pdk, work)
        (work / 'constraints.sdc').write_text(constraints())
    report = dict(status='running', full_chip_qualified=False, source_sha256=hashes,
                  memory_limit_mib=args.memory_mib,
                  geometric_only=args.geometric_only, bin_grid_count=args.bin_grid_count,
                  scope='Common digital placement, eight SRAM leaves; excludes analog/pads, CTS and routed signoff.')
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        start = time.monotonic()
        environment = dict(os.environ, UNIFIED_WORK=str(work), UNIFIED_PDK=str(pdk),
                           UNIFIED_NETLIST=str(args.proof.resolve().parent / 'mapped.v'), OMP_NUM_THREADS='1',
                           UNIFIED_GEOMETRIC_ONLY=str(int(args.geometric_only)),
                           UNIFIED_BIN_GRID=str(args.bin_grid_count))
        if resume:
            environment['UNIFIED_RESUME_DB'] = str(previous / 'floorplan.odb')
            report['resumed_from'] = str(args.resume_floorplan.resolve())
        with (work / 'placement.log').open('w') as log:
            execute(['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', str(tools / 'bin/openroad'), '-no_init', '-exit',
                     str(SOURCE / 'unified_place.tcl')], cwd=work, env=environment,
                    stdout=log, stderr=-2, timeout=14400, memory_exclusive=True, check=True)
        report['seconds'] = round(time.monotonic() - start, 3)
        inventory = json.loads((work / 'inventory.json').read_text())
        if inventory['movable_standard_cells'] != proof['movable_standard_cells']:
            raise ValueError('physical link lost mapped cells')
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('placement inputs changed')
        artifacts = ['placed.odb', 'placed.def', 'placed.v', 'constraints.sdc', 'route_tech.lef']
        if not args.geometric_only:
            artifacts.extend(('setup.rpt', 'hold.rpt', 'timing.rpt'))
        report.update(status='placed_unrouted', inventory=inventory,
            timing_analysis='not_run' if args.geometric_only else 'placement_estimate',
            artifact_sha256={str(work / n): digest(work / n) for n in artifacts})
        print(json.dumps(inventory), flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resume-floorplan', type=Path)
    parser.add_argument('--memory-mib', type=int, default=3072)
    parser.add_argument('--geometric-only', action='store_true',
                        help='save legal geometry before running timing/congestion optimization')
    parser.add_argument('--bin-grid-count', type=int, default=0)
    run(parser.parse_args())
