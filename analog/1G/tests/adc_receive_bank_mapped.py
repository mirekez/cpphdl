#!/usr/bin/env python3
"""Map the complete receiver controller and check 250 MHz slow-cell timing."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parent), str(ANALOG / '1G/IHPSG13G2')]
from hardware import configure_cleanup, execute
from mapped_area import library_areas, lef_areas


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def quote(path):
    if any(c in str(path) for c in ('"', '\\', '\n')):
        raise ValueError('unsupported synthesis path')
    return '"' + str(path) + '"'


def run(args):
    proof = json.loads(args.proof.read_text())
    if (proof.get('status') != 'pass' or proof.get('clocks') != 8320 or
            not all(proof.get(k) for k in ('continuous_and_gapped', 'changing_calibration', 'clipping',
                'unused_positions_checked', 'disable_flush_checked', 'reset_restart_checked', 'precharge_tested'))):
        raise ValueError('complete independently checked native/RTL receiver proof required')
    hashes = dict(proof['source_sha256']) | dict(proof['rtl_sha256'])
    binary = args.tools.resolve() / 'bin'
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty = library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    slow = library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'
    lef, tech = library / 'lef/sg13g2_stdcell.lef', library / 'lef/sg13g2_tech.lef'
    paths = [SOURCE, args.proof.resolve(), args.verilator.resolve(), liberty, slow, lef, tech,
        ANALOG / '1G/IHPSG13G2/mapped_area.py']
    paths += [binary / name for name in ('yosys', 'abc', 'sv2v', 'openroad')]
    paths += [args.tools.resolve() / 'root/usr/bin/openroad']
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('stale receive bank functional proof')
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    original, top = Path(proof['work']), 'ADCReceiveBankController'
    rtl = [Path(p) for p in proof['rtl_sha256']]
    report = dict(status='running', work=str(work), source_sha256=hashes, stages={},
        scope='13-slice controller/normalizer/tag composition, IHP mapped and repaired functional checks; slow cell-only timing with explicit output loads, no routed timing or complete analog PHY qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(label, argv, output=None):
        with (output or work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, stdout=log, stderr=-2, check=True, timeout=1200)

    def functional(stage, netlist):
        text, changes = re.subn(r'\bmodule\s+' + top + r'\s*\(', 'module ' + top + '_mapped(', netlist.read_text(), count=1)
        if changes != 1:
            raise ValueError('missing receive bank netlist top')
        flat = work / (stage + '_flat.v')
        flat.write_text(text)
        objects = work / (stage + '_obj')
        command(stage + '_compile', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
            '--flatten', '--output-groups', '0', '-j', '1', '--top-module', top, '-Wno-fatal', '--Mdir', objects,
            '-CFLAGS', '-DVERILATOR -std=c++17', flat, work / 'mapped_interface.sv', work / 'cells.v',
            SOURCE.with_name('adc_receive_bank.cpp'), '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
        trace = work / (stage + '.csv')
        command(stage + '_simulate', [objects / ('V' + top), trace])
        expected = original / 'native.csv'
        if digest(expected) != proof['native_trace_sha256'] or expected.read_bytes() != trace.read_bytes():
            raise ValueError(stage + ' receiver differs from independently checked native output')
        report['stages'][stage] = dict(status='pass', netlist_sha256=digest(netlist), trace_sha256=digest(trace), checked_clocks=8320)
        save()

    save()
    try:
        command('sv2v', [binary / 'sv2v', *rtl], work / 'design.v')
        (work / 'synth.ys').write_text(f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
chparam -set Slices 13 {top}
hierarchy -check -top {top}
synth -top {top} -flatten -noabc -noshare
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(binary / 'abc')} -liberty {quote(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(work / 'cells.v')}
''')
        command('synthesis', [binary / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'])
        module = json.loads((work / 'mapped.json').read_text())['modules'][top]
        cells = Counter(c['type'] for c in module['cells'].values())
        areas, footprints = library_areas(liberty.read_text()), lef_areas(lef.read_text())
        if not cells or any(not c.startswith('sg13g2_') or c not in areas or c not in footprints for c in cells):
            raise ValueError('receive bank retained unknown or non-IHP physical cells')
        for name, width in (('comparisons_in', 16), ('trial_out', 128), ('acquire_out', 16), ('convert_out', 16),
                            ('normalized_out', 8), ('normalized_slice_out', 4), ('gain_in', 16), ('offset_in', 8)):
            if len(module['ports'][name]['bits']) != width:
                raise ValueError('mapped receive bank lost a public interface bit: ' + name)
        ports = list(module['ports'])
        connections = ['.' + name + '(' + ('{' + ', '.join(f'{name}[{i}]' for i in range(16)) + '}'
            if name in ('trial_out', 'acquire_out', 'convert_out') else name) + ')' for name in ports]
        (work / 'mapped_interface.sv').write_text(f'''module {top}(
input wire clk, reset, enable_in, sample_enable_in, precharge_in,
input wire [15:0] comparisons_in,
input wire [7:0] offset_in,
input wire [15:0] gain_in,
output wire [7:0] trial_out[16],
output wire acquire_out[16], convert_out[16],
output wire [7:0] data_out, normalized_out,
output wire [3:0] slice_out, normalized_slice_out,
output wire valid_out, fault_out, normalized_valid_out, clipped_out);
{top}_mapped implementation(
''' + ',\n'.join(connections) + ');\nendmodule\n')
        report['mapped_cell_count'] = sum(cells.values())
        report['mapped_cell_footprint_um2'] = sum(footprints[name] * count for name, count in cells.items())
        report['mapped_liberty_area_um2'] = sum(areas[name] * count for name, count in cells.items())
        functional('mapped', work / 'mapped.v')
        for p in (work, tech, lef, slow):
            if any(c in str(p) for c in '{}\n'):
                raise ValueError('unsupported Tcl path')
        (work / 'timing.tcl').write_text(f'''read_lef {{{tech}}}
read_lef {{{lef}}}
read_liberty {{{slow}}}
read_verilog {{{work / 'mapped.v'}}}
link_design {top}
create_clock -name sample_clock -period 4 [get_ports clk]
set_clock_uncertainty 0.1 [get_clocks sample_clock]
set_input_delay 0.4 -clock sample_clock [all_inputs -no_clocks]
set_input_transition 0.1 [all_inputs]
set_output_delay 0.4 -clock sample_clock [all_outputs]
set_load 0.005 [all_outputs]
set controls [get_ports {{trial_out* acquire_out* convert_out*}}]
if {{[llength $controls] != 160}} {{error "incomplete analog control ports"}}
set_load 0.16 $controls
initialize_floorplan -die_area {{0 0 500 500}} -core_area {{20 20 480 480}} -site CoreSite
set_wire_rc -signal -layer Metal2
set_max_fanout 4 [current_design]
set_max_capacitance 0.18 [current_design]
set_max_transition 0.3 [current_design]
buffer_ports -outputs -buffer_cell sg13g2_buf_4
repair_design -pre_placement
write_verilog {{{work / 'resized.v'}}}
report_checks -path_delay max -group_path_count 3 -format full_clock_expanded
report_worst_slack -max
exit
''')
        command('timing', [binary / 'openroad', '-exit', '-no_init', work / 'timing.tcl'])
        text = (work / 'timing.log').read_text()
        matches = re.findall(r'worst slack max\s+([-+\d.eE]+)', text)
        slack = float(matches[0]) if len(matches) == 1 else None
        report['slow_cell_timing'] = dict(period_ns=4, setup_slack_ns=slack, input_output_delay_ns=.4,
            uncertainty_ns=.1, analog_control_load_pf=.16, other_output_load_pf=.005,
            corner='slow_1p08V_125C', stage='before placement, ideal clock; no routed/hold qualification')
        save()
        if slack is None or not math.isfinite(slack) or slack < 0 or '[ERROR' in text:
            raise ValueError('receive bank fails 250 MHz slow-cell timing')
        functional('repaired', work / 'resized.v')
        repaired = Counter(re.findall(r'^\s+(sg13g2_\w+)\s+(?:\\\S+|\w+)\s*\(', (work / 'resized.v').read_text(), re.M))
        if not repaired or any(name not in footprints for name in repaired):
            raise ValueError('timing repair introduced an unknown cell')
        report['repaired_cell_count'] = sum(repaired.values())
        report['repaired_cell_footprint_um2'] = sum(footprints[name] * count for name, count in repaired.items())
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('receive bank verification inputs changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir() if p.is_file() and p.name != 'checks.json'}
        report['status'] = 'pass'
        print('PASS complete IHP mapped/repaired receive controller, slow setup slack', slack,
            'ns, LEF footprint', report['repaired_cell_footprint_um2'], 'um2', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'tools', 'pdk', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
