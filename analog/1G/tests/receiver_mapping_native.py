"""Exercise constrained ABC mapping with actual Yosys and IHP Liberty cells."""
import argparse
import json
from pathlib import Path
import tempfile

from receiver_mapped import (configure_cleanup, execute, timing_mapping,
                             write_abc_constraints, synthesis_scripts)


def run(args):
    tools = args.tools.resolve() / 'bin'
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    with tempfile.TemporaryDirectory(prefix='receiver-mapping-') as directory:
        work = Path(directory)
        (work / 'design.v').write_text('''module PCSReceiver(input clk, input [4:0] a, output y, output reg [4:0] q);
wire [9:0] shifted = {a, a} >> a[2:0];
assign y = ~((a[0] & a[1]) | (a[2] & a[3]) | a[4]) ^ shifted[0];
always @(posedge clk) q <= q + a;
endmodule
''')
        options = write_abc_constraints(work, timing_mapping(3000.0, 15.0))
        text = ''
        for name, script, checkpoint in synthesis_scripts(work, library, tools, options):
            path = work / (name + '.ys')
            path.write_text(script)
            with (work / (name + '.log')).open('w') as log:
                result = execute(['prlimit', '--as=536870912', '--core=0', '--',
                                  tools / 'yosys', '-Q', '-T', '-s', path],
                                 cwd=work, stdout=log, stderr=-2, timeout=60,
                                 memory_exclusive=False)
            text += (work / (name + '.log')).read_text()
            if result.returncode or not (work / checkpoint).is_file():
                raise ValueError('checkpointed IHP mapping failed:\n' + text)
        if 'read_constr -v' not in text or 'upsize -D 3000' not in text:
            raise ValueError('ABC did not execute the constrained timing script:\n' + text)
        module = json.loads((work / 'mapped.json').read_text())['modules']['PCSReceiver']
        if not module['cells'] or any(not c['type'].startswith('sg13g2_') for c in module['cells'].values()):
            raise ValueError('fixture did not map to actual IHP gates')
        if not any(c['type'].startswith('sg13g2_df') for c in module['cells'].values()):
            raise ValueError('checkpoint reload lost the sequential circuit')
        # Check the actual recursively lowered circuit against the source,
        # including sequential behavior, before library-specific FF mapping.
        proof = work / 'equivalence.ys'
        proof.write_text('''read_verilog design.v
prep -top PCSReceiver
rename PCSReceiver gold
read_ilang lowered.rtlil
rename PCSReceiver gate
miter -equiv -flatten gold gate miter
prep -top miter
sat -verify -seq 8 -set-init-zero -prove trigger 0
''')
        with (work / 'equivalence.log').open('w') as log:
            execute(['prlimit', '--as=536870912', '--core=0', '--',
                     tools / 'yosys', '-Q', '-T', '-s', proof], cwd=work,
                    stdout=log, stderr=-2, timeout=60, check=True, memory_exclusive=False)
        (work / 'bad.v').write_text((work / 'design.v').read_text().replace(
            'assign y = ~', "assign y = 1'b1 ^ ~"))
        proof.write_text(proof.read_text().replace('read_verilog design.v', 'read_verilog bad.v'))
        with (work / 'negative.log').open('w') as log:
            negative = execute(['prlimit', '--as=536870912', '--core=0', '--',
                                tools / 'yosys', '-Q', '-T', '-s', proof], cwd=work,
                               stdout=log, stderr=-2, timeout=60, memory_exclusive=False)
        if not negative.returncode or 'proof did fail' not in (work / 'negative.log').read_text():
            raise ValueError('lowering equivalence did not reject an inverted output')
        print('PASS checkpointed IHP ABC mapping: frontend/technology reload, driver, load and delay target')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('tools', 'pdk'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
