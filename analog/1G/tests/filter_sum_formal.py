#!/usr/bin/env python3
"""Prove the converted filter adder against an independent signed sum."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from hardware import configure_cleanup, execute


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    rtl, work, tools = args.rtl.resolve(), args.work.resolve(), args.tools.resolve() / 'bin'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    functions = re.findall(r'\bfunction\s+logic\[31:0\]\s+sum_products\s*\(.*?\bendfunction\b',
                           rtl.read_text(), re.S)
    if len(functions) != 1:
        raise ValueError('one converted four-product sum function required')
    inputs = [Path(__file__).resolve(), rtl, tools / 'sv2v', tools / 'yosys']
    inputs += [path for path in (args.tools.resolve() / 'root/usr/bin' / name
               for name in ('yosys', 'sv2v')) if path.is_file()]
    hashes = {str(path): digest(path) for path in inputs}
    report = dict(status='running', source_sha256=hashes,
                  scope='all signed 26-bit product combinations; converted combinational sum only')
    (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    try:
        (work / 'proof.sv').write_text('''module ProductSumProof(
    input logic [25:0] a, b, c, d, output logic good);
''' + functions[0] + '''
    wire signed [27:0] sa = {{2{a[25]}}, a};
    wire signed [27:0] sb = {{2{b[25]}}, b};
    wire signed [27:0] sc = {{2{c[25]}}, c};
    wire signed [27:0] sd = {{2{d[25]}}, d};
    wire signed [27:0] expected = sa + sb + sc + sd;
    wire [31:0] actual = sum_products({{6{a[25]}}, a}, {{6{b[25]}}, b},
                                      {{6{c[25]}}, c}, {{6{d[25]}}, d});
    assign good = actual == {4'b0, expected};
endmodule
''')
        with (work / 'proof.v').open('w') as output:
            execute([tools / 'sv2v', work / 'proof.sv'], stdout=output,
                    check=True, timeout=30, memory_exclusive=False)
        for label, value, passed in (('equivalence', 1, True), ('negative_control', 0, False)):
            script = work / (label + '.ys')
            script.write_text('read_verilog proof.v\nprep -top ProductSumProof\n'
                              f'sat -verify -prove good {value} -show-inputs\n')
            with (work / (label + '.log')).open('w') as log:
                result = execute(['prlimit', '--as=536870912', '--core=0', '--',
                                  tools / 'yosys', '-Q', '-T', '-s', script],
                                 cwd=work, stdout=log, stderr=-2,
                                 timeout=180, memory_exclusive=False)
            text = (work / (label + '.log')).read_text()
            expected = 'SUCCESS!' if passed else 'proof did fail'
            if (result.returncode == 0) != passed or expected not in text:
                raise ValueError(label + ' did not establish the expected formal result')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('formal inputs changed')
        report.update(status='pass', input_bits=104, negative_control_rejected=True)
        print('PASS converted DSP sum: all four signed 26-bit products; negative control rejected')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(path): digest(path) for path in work.iterdir()
                                    if path.is_file() and path.name != 'checks.json'}
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('rtl', 'work', 'tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
