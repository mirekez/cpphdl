"""Exercise constrained ABC mapping with actual Yosys and IHP Liberty cells."""
import argparse
import json
from pathlib import Path
import tempfile

from receiver_mapped import configure_cleanup, execute, timing_mapping, write_abc_constraints


def run(args):
    tools = args.tools.resolve() / 'bin'
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    with tempfile.TemporaryDirectory(prefix='receiver-mapping-') as directory:
        work = Path(directory)
        (work / 'test.v').write_text('''module fixture(input [4:0] a, output y);
assign y = ~((a[0] & a[1]) | (a[2] & a[3]) | a[4]);
endmodule
''')
        options = write_abc_constraints(work, timing_mapping(3000.0, 15.0))
        (work / 'test.ys').write_text(f'''read_liberty -lib "{library}"
read_verilog test.v
synth -top fixture -noabc
abc -exe "{tools / 'abc'}" -liberty "{library}"{options}
clean
check -assert
write_json mapped.json
''')
        with (work / 'run.log').open('w') as log:
            result = execute([tools / 'yosys', '-Q', '-T', '-s', work / 'test.ys'],
                             cwd=work, stdout=log, stderr=-2, timeout=60)
        text = (work / 'run.log').read_text()
        if result.returncode:
            raise ValueError('constrained ABC mapping failed:\n' + text)
        if 'read_constr -v' not in text or 'upsize -D 3000' not in text:
            raise ValueError('ABC did not execute the constrained timing script:\n' + text)
        module = json.loads((work / 'mapped.json').read_text())['modules']['fixture']
        if not module['cells'] or any(not c['type'].startswith('sg13g2_') for c in module['cells'].values()):
            raise ValueError('fixture did not map to actual IHP gates')
        print('PASS constrained IHP ABC mapping: local constraint file, driver, load and delay target')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('tools', 'pdk'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
