#!/usr/bin/env python3
"""Exercise routing capacitance margins in real OpenROAD RC estimation."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def check(args):
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    with tempfile.TemporaryDirectory(prefix='cpphdl-routing-rc-') as temporary:
        work = Path(temporary)
        (work / 'design.v').write_text('module Margin(input A, output Y);\n'
            'wire mid; sg13g2_buf_1 a(.A(A), .X(mid));\n'
            'sg13g2_buf_1 b(.A(mid), .X(Y)); endmodule\n')
        script = f'''set_thread_count 1
read_lef {{{library}/lef/sg13g2_tech.lef}}
read_lef {{{library}/lef/sg13g2_stdcell.lef}}
read_liberty {{{library}/lib/sg13g2_stdcell_slow_1p08V_125C.lib}}
read_verilog {{{work}/design.v}}
link_design Margin
initialize_floorplan -die_area {{0 0 80 80}} -core_area {{10 10 70 70}} -site CoreSite
set_wire_rc -signal -layer Metal3
set_wire_rc -clock -layer Metal5
set_max_capacitance 0.1 [current_design]
set_max_transition 0.5 [current_design]
set_max_fanout 8 [current_design]
make_tracks
place_pins -hor_layers Metal3 -ver_layers Metal2
global_placement -density 0.3
detailed_placement
set_routing_layers -signal Metal2-Metal5 -clock Metal3-Metal5
global_route
estimate_parasitics -global_routing -spef_file {{{work}/nominal.spef}}
write_sdc {{{work}/before.sdc}}
source {{{SOURCE}/routing_rc_margin.tcl}}
foreach invalid {{0 0.99 5.01 NaN Inf bad}} {{
    if {{![catch {{apply_routing_capacitance_margin $invalid {{{library}/lef/sg13g2_tech.lef}}}}]}} {{error "accepted invalid margin"}}
}}
foreach {{name factor}} {{margin 2.5 repeated 2.5 restored 1}} {{
    apply_routing_capacitance_margin $factor {{{library}/lef/sg13g2_tech.lef}}
    estimate_parasitics -global_routing -spef_file "{work}/$name.spef"
}}
write_sdc {{{work}/after.sdc}}
'''
        (work / 'test.tcl').write_text(script)
        result = subprocess.run([str(args.tools.resolve() / 'bin/openroad'), '-no_init', '-exit',
            str(work / 'test.tcl')], capture_output=True, text=True, timeout=30)
        if result.returncode or '[ERROR' in result.stdout:
            raise ValueError(result.stdout + result.stderr)
        def caps(name):
            text = (work / (name + '.spef')).read_text()
            return {net: float(value) for net, value in re.findall(r'^\*D_NET (\S+) (\S+)', text, re.M)}
        nominal, margin, repeated, restored = [caps(name) for name in ('nominal', 'margin', 'repeated', 'restored')]
        if not nominal or nominal.keys() != margin.keys() or margin != repeated or nominal != restored:
            raise ValueError('margin lost a net, compounded, or failed to restore nominal RC')
        for net, value in nominal.items():
            if value <= 0 or abs(margin[net] / value - 2.5) > 1e-4:
                raise ValueError('global-route capacitance did not follow the requested margin')
        def resistance(name):
            text = (work / (name + '.spef')).read_text()
            return [float(line.split()[-1]) for section in re.findall(r'\*RES\n(.*?)\*END', text, re.S)
                    for line in section.splitlines() if line.strip()]
        original_resistance = resistance('nominal')
        if not original_resistance or max(original_resistance) <= 0:
            raise ValueError('fixture has no wire resistance')
        for name in ('margin', 'repeated', 'restored'):
            if resistance(name) != original_resistance:
                raise ValueError('capacitance optimization changed wire resistance')
        def constraints(name):
            # write_sdc includes a wall-clock timestamp in its comment header.
            return [line.strip() for line in (work / name).read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith('#')]
        if constraints('before.sdc') != constraints('after.sdc'):
            raise ValueError('optimization margin changed acceptance constraints')
        print('PASS: actual global-route RC scales, repeats and restores; constraints unchanged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    check(parser.parse_args())
