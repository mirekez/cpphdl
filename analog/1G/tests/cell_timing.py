#!/usr/bin/env python3
"""Necessary pre-route timing check with actual SG13G2 slow-corner cells."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
from hardware import configure_cleanup, execute


def run(args):
    work = args.work.resolve()
    report = work / 'cell_timing.json'
    report.unlink(missing_ok=True)
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    inputs = [work / 'mapped.v', library / 'lef/sg13g2_tech.lef',
              library / 'lef/sg13g2_stdcell.lef', library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib']
    if not math.isfinite(args.period) or args.period <= 0 or not all(p.is_file() for p in inputs):
        raise ValueError('invalid timing contract or missing mapped/PDK inputs')
    if args.require_functional:
        evidence = json.loads((work / 'hardware_checks.json').read_text())
        if (evidence.get('status') != 'pass' or not evidence.get('native_mapped_exact') or
                evidence.get('module') != args.module or
                evidence.get('mapped_sha256') != hashlib.sha256(inputs[0].read_bytes()).hexdigest()):
            raise ValueError('mapped functional proof is absent, failed or stale')
        if not evidence.get('source_sha256') or any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != value
                for path, value in evidence['source_sha256'].items()):
            raise ValueError('functional source provenance is absent or stale')
        inputs.append(work / 'hardware_checks.json')
    if any(any(x in str(p) for x in '{}\n') for p in inputs):
        raise ValueError('unsupported Tcl path')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', args.module):
        raise ValueError('invalid top module')
    script = f'''read_lef {{{inputs[1]}}}
read_lef {{{inputs[2]}}}
read_liberty {{{inputs[3]}}}
read_verilog {{{inputs[0]}}}
link_design {args.module}
create_clock -name sample_clock -period {args.period} [get_ports clk]
set_clock_uncertainty 0.1 [get_clocks sample_clock]
set_input_delay 0.4 -clock sample_clock [all_inputs -no_clocks]
set_input_transition 0.1 [all_inputs]
set_output_delay 0.4 -clock sample_clock [all_outputs]
set_load 0.005 [all_outputs]
report_checks -path_delay max -group_path_count 3 -format full_clock_expanded
report_worst_slack -max
exit
'''
    (work / 'cell_timing.tcl').write_text(script)
    if args.repair:
        (work / 'resized.v').unlink(missing_ok=True)
        repair = '''initialize_floorplan -die_area {0 0 1600 1600} -core_area {20 20 1580 1580} -site CoreSite
set_wire_rc -signal -layer Metal2
set_max_fanout 4 [current_design]
set_max_capacitance 0.01 [current_design]
set_max_transition 0.3 [current_design]
repair_design -pre_placement
''' + 'write_verilog {' + str(work / 'resized.v') + '}\n'
        script = script.replace('report_checks -path_delay max', repair + 'report_checks -path_delay max')
        (work / 'cell_timing.tcl').write_text(script)
    with (work / 'cell_timing.log').open('w') as log:
        result = execute([str(args.tools.resolve() / 'bin/openroad'), '-exit', '-no_init',
                          str(work / 'cell_timing.tcl')], stdout=log, stderr=-2, timeout=120,
                         memory_exclusive=False) # Cell-only STA is bounded; routing/extraction owns the large memory slot.
    text = (work / 'cell_timing.log').read_text()
    inputs.append(work / 'cell_timing.tcl')
    if args.repair and (work / 'resized.v').is_file():
        inputs.append(work / 'resized.v')
    slack = re.findall(r'worst slack max\s+([-+\d.eE]+)', text)
    value = float(slack[0]) if len(slack) == 1 else None
    passed = result.returncode == 0 and value is not None and value >= 0 and '[ERROR' not in text
    evidence = dict(status='pass' if passed else 'fail', module=args.module,
                    period_ns=args.period, setup_slack_ns=value,
                    functional_evidence_required=args.require_functional,
                    corner='slow_1p08V_125C', input_output_delay_ns=0.4,
                    preplacement_fanout_capacitance_repair=args.repair,
                    repair_limits=dict(fanout=4, capacitance_pf=0.01, transition_ns=0.3) if args.repair else None,
                    clock_uncertainty_ns=0.1, output_load_pf=0.005,
                    scope='cell-only estimate; no routed wires, hold, CDC, IR/EM or full-chip signoff',
                    input_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    report.write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--period', type=float, default=8)
    parser.add_argument('--repair', action='store_true',
                        help='insert/size real cells for fanout/capacitance; still not routed timing')
    parser.add_argument('--require-functional', action='store_true',
                        help='require matching native/mapped functional evidence for this exact netlist')
    raise SystemExit(run(parser.parse_args()))
