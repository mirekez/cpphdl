#!/usr/bin/env python3
"""Prove admission factoring and reject unexpected mapped topology."""
import argparse
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    tools = args.tools.resolve() / 'bin'
    with tempfile.TemporaryDirectory(prefix='cpphdl-clock-output-') as temporary:
        work = Path(temporary)
        for case in ('valid', 'wrong_top', 'wrong_function', 'wrong_inputs', 'collision'):
            top = 'Other' if case == 'wrong_top' else 'ProcessingClockControl'
            gate = 'sg13g2_or3_1' if case == 'wrong_function' else 'sg13g2_and3_1'
            qualifier = 'ready' if case == 'wrong_inputs' else 'qualified_ready'
            instance = 'admission_driver' if case == 'collision' else 'output_gate'
            (work / 'design.v').write_text(f'''module {top}(
input enable_in, ready, qualified_ready, output ready_pcs_out);
{gate} {instance}(.A(enable_in), .B(ready), .C({qualifier}), .X(ready_pcs_out));
endmodule
''')
            checks = '''
if {$failed} {error "valid factoring failed: $message"}
if {[llength [get_cells *]] != 3} {error "unexpected factored inventory"}
write_verilog after.v
''' if case == 'valid' else '''
if {!$failed} {error "invalid factoring accepted"}
if {[llength [get_cells *]] != $before} {error "rejection mutated cells"}
if {[llength [get_nets *]] != $nets_before} {error "rejection mutated nets"}
'''
            (work / 'test.tcl').write_text(f'''set_thread_count 1
read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
read_liberty {{{library}/lib/sg13g2_stdcell_typ_1p20V_25C.lib}}
read_verilog design.v
link_design {top}
source {{{SOURCE}/processing_clock_output.tcl}}
set before [llength [get_cells *]]
set nets_before [llength [get_nets *]]
set failed [catch {{prepare_processing_clock_output}} message]
{checks}
puts "PASS: {case}"
''')
            result = subprocess.run(['/usr/bin/prlimit', '--as=1073741824', '--',
                                     str(tools / 'openroad'), '-no_init', '-exit', 'test.tcl'],
                                    cwd=work, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            if result.returncode or '[ERROR' in output or f'PASS: {case}' not in output:
                raise ValueError(output)
            if case == 'valid':
                (work / 'proof.v').write_text('''module proof(input enable_in, ready, qualified_ready, output equal);
wire actual;
ProcessingClockControl dut(enable_in, ready, qualified_ready, actual);
assign equal = actual == (enable_in & ready & qualified_ready);
endmodule
''')
                (work / 'proof.ys').write_text(f'''read_liberty -ignore_miss_func -ignore_miss_dir {library}/lib/sg13g2_stdcell_typ_1p20V_25C.lib
read_verilog after.v proof.v
hierarchy -check -top proof
flatten
proc
opt
sat -verify -prove equal 1 -show-inputs
''')
                result = subprocess.run(['/usr/bin/prlimit', '--as=1073741824', '--',
                                         str(tools / 'yosys'), '-Q', '-T', '-s', 'proof.ys'],
                                        cwd=work, capture_output=True, text=True, timeout=30)
                if result.returncode or 'SUCCESS' not in result.stdout:
                    raise ValueError(result.stdout + result.stderr)
            print('PASS: ' + case)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    check(parser.parse_args())
