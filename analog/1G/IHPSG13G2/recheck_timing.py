#!/usr/bin/env python3
"""Re-run routed STA without changing a verified module's netlist or masks."""
import argparse
import json
from pathlib import Path
import shutil
import sys

SOURCE = Path(__file__).resolve().parent
COMMON = SOURCE.parents[1] / 'pdk/1G_repeater_IHPSG13G2'
sys.path.insert(0, str(COMMON))
sys.path.insert(0, str(SOURCE))
from checks import check_timing
from tool_process import configure_cleanup, execute
from physical_module import MODULES, digest
from processing_clock_constraints import constraints as processing_clock_constraints
from phy_reset_constraints import constraints as phy_reset_constraints


def timing_contract(evidence):
    """Preserve the implemented interface instead of substituting defaults."""
    constraints = evidence.get('constraints', {})
    keys = ('period_ns', 'clock_uncertainty_ns', 'input_output_delay_ns',
            'accepted_transition_ns', 'accepted_fanout', 'accepted_capacitance_pf',
            'analog_control_load_pf')
    if any(key not in constraints or not isinstance(constraints[key], (int, float))
           or not 0 < constraints[key] < float('inf') for key in keys):
        raise ValueError('original implementation timing constraints are incomplete or invalid')
    analog = ''
    if evidence.get('top') not in MODULES:
        raise ValueError('unknown module timing interface')
    module = MODULES[evidence['top']]
    if evidence['top'] == 'PHYResetControl':
        if (constraints.get('cross_domain_timing') != 'excluded' or
                any(key not in constraints for key in ('reference_period_ns', 'pcs_period_ns', 'output_load_pf'))):
            raise ValueError('original reset clock-domain constraints are incomplete')
        script = phy_reset_constraints(constraints['reference_period_ns'], constraints['period_ns'],
            constraints['pcs_period_ns'], constraints['clock_uncertainty_ns'],
            constraints['input_output_delay_ns'], constraints['output_load_pf'])
        script += f"set_max_transition {constraints['accepted_transition_ns']} [current_design]\n"
        script += f"set_max_fanout {constraints['accepted_fanout']} [current_design]\n"
        script += f"set_max_capacitance {constraints['accepted_capacitance_pf']} [current_design]\n"
        return constraints.copy(), script
    if evidence['top'] == 'ProcessingClockControl':
        if constraints.get('cross_domain_timing') != 'excluded' or 'reference_period_ns' not in constraints:
            raise ValueError('original clock-domain constraints are incomplete')
        script = processing_clock_constraints(constraints['reference_period_ns'], constraints['period_ns'],
            constraints['clock_uncertainty_ns'], constraints['input_output_delay_ns'],
            constraints.get('ready_output_load_pf', constraints['analog_control_load_pf']))
        script += f"set_max_transition {constraints['accepted_transition_ns']} [current_design]\n"
        script += f"set_max_fanout {constraints['accepted_fanout']} [current_design]\n"
        script += f"set_max_capacitance {constraints['accepted_capacitance_pf']} [current_design]\n"
        return constraints.copy(), script
    if evidence['top'] == 'ADCReceiveController' or module.get('array_interface', False):
        patterns = module.get('analog_control_patterns', 'trial_out* acquire_out* convert_out*')
        count = module.get('analog_control_ports', 160)
        analog = f'''set analog_controls [get_ports {{{patterns}}}]
if {{[llength $analog_controls] != {count}}} {{ error "ADC analog-control interface is incomplete" }}
set_load {constraints['analog_control_load_pf']} $analog_controls
'''
    return constraints.copy(), f'''create_clock -name sample_clock -period {constraints['period_ns']} [get_ports clk]
set_clock_uncertainty {constraints['clock_uncertainty_ns']} [all_clocks]
set_input_delay {constraints['input_output_delay_ns']} -clock sample_clock [all_inputs -no_clocks]
set_input_transition 0.1 [all_inputs]
set_output_delay {constraints['input_output_delay_ns']} -clock sample_clock [all_outputs]
set_load 0.005 [all_outputs]
{analog}set_max_transition {constraints['accepted_transition_ns']} [current_design]
set_max_fanout {constraints['accepted_fanout']} [current_design]
set_max_capacitance {constraints['accepted_capacitance_pf']} [current_design]
'''


def recheck(args):
    implementation, work = args.implementation.resolve(), args.work.resolve()
    original = implementation / 'physical_module.json'
    evidence = json.loads(original.read_text())
    if evidence.get('status') not in ('failed', 'component_checks_pass') or evidence.get('top') not in MODULES:
        raise ValueError('a completed physical module implementation is required')
    if not evidence.get('native_postroute_exact'):
        raise ValueError('routed logic has no exact native comparison')
    # The routed netlist, extracted RC and exact GDS must be the original files.
    for name in ('postroute.v', 'core.spef', 'module_filled.gds'):
        if evidence.get('artifact_sha256', {}).get(name) != digest(implementation / name):
            raise ValueError('changed or untracked implementation artifact: ' + name)
    proof = json.loads((implementation / 'hardware_checks.json').read_text())
    if (proof.get('status') != 'pass' or not proof.get('native_mapped_exact') or
            proof.get('module') != evidence['top'] or not proof.get('source_sha256')):
        raise ValueError('mapped functional evidence is missing')
    if any(digest(Path(path)) != value for path, value in proof['source_sha256'].items()):
        raise ValueError('C++ module/harness source changed since implementation')
    if digest(implementation / 'cpp_trace.csv') != digest(implementation / 'postroute_trace.csv'):
        raise ValueError('routed and native trajectories differ')
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('timing recheck work directory must be fresh')
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib'
    slow = library / 'sg13g2_stdcell_slow_1p08V_125C.lib'
    fast = library / 'sg13g2_stdcell_fast_1p32V_m40C.lib'
    tech = library.parent / 'lef/sg13g2_tech.lef'
    cells = library.parent / 'lef/sg13g2_stdcell.lef'
    for path in (slow, fast, tech, cells):
        if evidence.get('input_sha256', {}).get(str(path)) != digest(path):
            raise ValueError('IHP timing library changed since implementation')
    netlist, spef = implementation / 'postroute.v', implementation / 'core.spef'
    shutil.copyfile(spef, work / 'core.spef')
    paths = (slow, fast, tech, cells, netlist, spef, work)
    if any(any(char in str(path) for char in '{}\n') for path in paths):
        raise ValueError('unsupported Tcl path')
    script = work / 'recheck.tcl'
    constraints, contract = timing_contract(evidence)
    script.write_text(f'''read_lef {{{tech}}}
read_lef {{{cells}}}
define_corners slow fast
read_liberty -corner slow {{{slow}}}
read_liberty -corner fast {{{fast}}}
read_verilog {{{netlist}}}
link_design {evidence['top']}
{contract}
set_propagated_clock [all_clocks]
read_spef -corner slow {{{spef}}}
read_spef -corner fast {{{spef}}}
report_checks -path_delay min_max -format full_clock_expanded -digits 9 > {{{work / 'timing.rpt'}}}
report_worst_slack -max -digits 9 > {{{work / 'setup.rpt'}}}
report_worst_slack -min -digits 9 > {{{work / 'hold.rpt'}}}
report_check_types -max_slew -max_capacitance -max_fanout -violators > {{{work / 'electrical.rpt'}}}
write_sdc {{{work / 'physical.sdc'}}}
exit
''')
    inputs = [Path(__file__).resolve(), SOURCE / 'physical_module.py', COMMON / 'checks.py',
              COMMON / 'tool_process.py', original, script, slow, fast, tech, cells, netlist, spef, work / 'core.spef',
              implementation / 'module_filled.gds', implementation / 'hardware_checks.json']
    provenance = {str(path): digest(path) for path in inputs}
    report = dict(status='running', top=evidence['top'], original_implementation=str(original),
                  scope='routed STA recheck only; no reroute, new DRC/LVS or full-chip claim',
                  constraints=constraints,
                  input_sha256=provenance)
    try:
        with (work / 'openroad.log').open('w') as log:
            result = execute([str(args.tools.resolve() / 'bin/openroad'), '-no_init', '-exit', str(script)],
                             stdout=log, stderr=-2, timeout=120)
        if result.returncode or '[ERROR' in (work / 'openroad.log').read_text():
            raise ValueError('routed timing analysis failed')
        report['timing'] = check_timing(work)
        if any(digest(Path(path)) != value for path, value in provenance.items()):
            raise ValueError('implementation/constraints changed during STA')
        report['status'] = 'routed_timing_pass'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (work / 'timing_recheck.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['timing'], indent=2))


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--implementation', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=SOURCE.parents[1] / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=SOURCE.parents[1] / 'out/repeater-toolchain')
    recheck(parser.parse_args())
