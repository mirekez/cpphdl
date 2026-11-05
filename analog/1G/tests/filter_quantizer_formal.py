#!/usr/bin/env python3
"""Prove bounded quantization preserves output and error for every input state."""
import argparse
import json
from pathlib import Path
import re

from filter_sum_formal import digest
from hardware import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = args.work.resolve()
    tools = args.tools.resolve() / 'bin'
    functions = re.findall(r'\bfunction\s+logic\s+signed\[31:0\]\s+bounded_quantized_value\s*\(.*?\bendfunction\b',
                           args.rtl.read_text(), re.S)
    if len(functions) != 1:
        raise ValueError('requires the actual converted quantizer function')
    files = [Path(__file__), Path(__file__).with_name('filter_sum_formal.py'), args.rtl,
             tools / 'sv2v', tools / 'yosys']
    files += [p for p in (args.tools.resolve() / 'root/usr/bin' / name for name in ('yosys', 'sv2v')) if p.is_file()]
    hashes = {str(p.resolve()): digest(p) for p in files}
    report = dict(status='running', source_sha256=hashes, input_bits=36,
                  scope='all 31-bit accumulators, four modes and eight supervised targets; unchanged output and saturated error')
    try:
        (work / 'proof.sv').write_text('''module QuantizerProof(
    input logic [30:0] accumulated,
    input logic [1:0] mode,
    input logic [2:0] desired,
    output logic good);
''' + functions[0] + '''
    function automatic signed [31:0] target(input signed [31:0] value,
                                            input [1:0] mode, input [2:0] desired);
        case (mode)
            1: target = 0;
            2: target = value < -32 ? -64 : value > 32 ? 64 : 0;
            3: target = value < -48 ? -64 : value < -16 ? -32 : value > 48 ? 64 : value > 16 ? 32 : 0;
            default: target = ($signed({29'b0, desired}) - 2) * 32;
        endcase
    endfunction
    function automatic [7:0] sample(input signed [31:0] value);
        sample = value < -128 ? 0 : value > 127 ? 255 : value + 128;
    endfunction
    function automatic [8:0] clipped_error(input signed [31:0] value,
                                           input [1:0] mode, input [2:0] desired);
        reg signed [31:0] error;
        error = target(value, mode, desired) - value;
        clipped_error = error < -255 ? -255 : error > 255 ? 255 : error;
    endfunction
    wire signed [31:0] signed_total = {accumulated[30], accumulated};
    // Signed division by positive 16384 specifies truncation toward zero.
    wire signed [31:0] reference = signed_total / 32'sd16384;
    wire signed [31:0] actual = bounded_quantized_value({1'b0, accumulated});
    assign good = sample(reference) == sample(actual) &&
                  clipped_error(reference, mode, desired) == clipped_error(actual, mode, desired);
endmodule
''')
        with (work / 'proof.v').open('w') as output:
            execute([tools / 'sv2v', work / 'proof.sv'], stdout=output, check=True, timeout=30,
                    memory_exclusive=False)
        for label, value, passed in (('equivalence', 1, True), ('negative_control', 0, False)):
            script = work / (label + '.ys')
            script.write_text('read_verilog proof.v\nprep -top QuantizerProof\n'
                              f'sat -verify -prove good {value} -show-inputs\n')
            with (work / (label + '.log')).open('w') as log:
                result = execute(['prlimit', '--as=536870912', '--core=0', '--', tools / 'yosys',
                                  '-Q', '-T', '-s', script], cwd=work, stdout=log, stderr=-2,
                                  timeout=180, memory_exclusive=False)
            message = (work / (label + '.log')).read_text()
            if ((result.returncode == 0) != passed or
                    ('SUCCESS!' if passed else 'proof did fail') not in message):
                raise ValueError(label + ' did not complete the expected proof')
        if any(digest(Path(name)) != expected for name, expected in hashes.items()):
            raise ValueError('formal inputs changed')
        report.update(status='pass', negative_control_rejected=True)
        print('PASS quantizer: all 2^36 input states, unchanged output/error; negative control rejected')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
                                    if p.is_file() and p.name != 'checks.json'}
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('rtl', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
