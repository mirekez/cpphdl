"""Prove parameterized children still work after separate synthesis and linking."""
import argparse
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tool_process import configure_cleanup, execute
from unified_synthesis import quote, split_modules


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'source.v'
    source.write_text('''module ControlCrossing #(parameter Width = 2)
    (input [Width-1:0] a, output [Width-1:0] b);
    assign b = a + 1'b1;
endmodule
module SynchronousFrameFIFO(input a, output b);
    assign b = a;
endmodule
module RepeaterDigital(input [5:0] a, output correct);
    wire [1:0] small;
    wire [3:0] large;
    wire forwarded;
    ControlCrossing #(.Width(2)) first(a[1:0], small);
    ControlCrossing #(.Width(4)) second(a[5:2], large);
    SynchronousFrameFIFO fifo(a[0], forwarded);
    assign correct = (small == ((a[1:0] + 2'd1) & 2'b11)) &&
                     (large == ((a[5:2] + 4'd1) & 4'b1111)) && (forwarded == a[0]);
endmodule
''')

    def yosys(name, commands):
        script = work / (name + '.ys')
        script.write_text(commands)
        with (work / (name + '.log')).open('w') as log:
            execute([str(args.yosys.resolve()), '-s', str(script)],
                    cwd=work, stdout=log, stderr=-2, check=True, timeout=30,
                    memory_exclusive=True)

    yosys('elaborate', f'''read_verilog {quote(source)}
hierarchy -check -top RepeaterDigital
write_ilang {quote(work / 'elaborated.il')}
blackbox
write_ilang {quote(work / 'interfaces.il')}
''')
    modules = split_modules((work / 'elaborated.il').read_text())
    widths = []
    for name, body in modules.items():
        if 'ControlCrossing' in name:
            widths.append(int(re.search(r'wire width (\d+) input \d+ \\a\n', body)[1]))
    if sorted(widths) != [2, 4]:
        raise ValueError('lost distinct parameterized widths')
    mapped = []
    for index, (name, body) in enumerate(modules.items()):
        path = work / f'unit{index}.il'
        path.write_text(body)
        output = work / f'mapped{index}.il'
        yosys(f'unit{index}', f'''read_ilang -lib {quote(work / 'interfaces.il')}
read_ilang -overwrite {quote(path)}
hierarchy -check -top {name}
proc
opt
select {name}
write_ilang -selected {quote(output)}
''')
        mapped.append(output)
    yosys('prove', ''.join(f'read_ilang {quote(p)}\n' for p in mapped) + '''
hierarchy -check -top RepeaterDigital
flatten
opt
check -assert
sat -verify -prove correct 1 -show-inputs
''')
    print('PASS: separate 2-bit/4-bit mapping and linked behavior for all 64 inputs')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--yosys', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
