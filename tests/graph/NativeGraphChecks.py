#!/usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='graph-', dir=args.work) as temporary:
        root = Path(temporary)

        def run(command, success=True):
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=300)
            if (result.returncode == 0) != success:
                logs = '\n'.join(str(path) + '\n' + path.read_text()[-4000:] for path in root.rglob('*.log'))
                raise AssertionError((command, result.returncode, result.stdout, result.stderr, logs))
            return result

        source = root / 'NativeGraph.sv'
        shutil.copyfile(fixtures / source.name, source)
        generated = root / 'graph.cc'
        command = [args.hdlcpp, '--native-graph', '--top', 'NativeGraph', '--output', generated, source]
        run(command)
        before = generated.read_bytes()
        run(command, success=False)
        assert generated.read_bytes() == before
        source.unlink()
        backend = root / 'backend'
        run([args.cpphdl, '--native-graph', '--output', backend, '--cxx', args.cxx,
             '--runner', fixtures / 'NativeGraphRun.cc', generated, '--', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer'])
        print(run([backend / 'run']).stdout, end='')
        mutated = root / 'mutated.cc'
        mutated.write_text(generated.read_text().replace('graph.emit(argv[1]);',
            'for(auto& port : graph.ports) if(!port.input) std::fill(port.bits.begin(), port.bits.end(), 0); graph.emit(argv[1]);'))
        changed = root / 'changed'
        run([args.cpphdl, '--native-graph', '--output', changed, '--cxx', args.cxx,
             '--runner', fixtures / 'NativeGraphRun.cc', mutated])
        run([changed / 'run'], success=False)
        for name, body in [
            ('latch', 'module bad(input logic a, b, output logic y); always_comb if(a) y=b; endmodule'),
            ('cycle', 'module bad(input logic a, output logic y); assign y=~y ^ a; endmodule'),
            ('derived', 'module bad(input logic clk, output logic y); logic divided; always_ff @(posedge clk) divided<=~divided; always_ff @(posedge divided) y<=~y; endmodule'),
        ]:
            rtl = root / (name + '.sv')
            rtl.write_text(body)
            cpp = root / (name + '.cc')
            run([args.hdlcpp, '--native-graph', '--top', 'bad', '--output', cpp, rtl])
            run([args.cpphdl, '--native-graph', '--output', root / name, '--cxx', args.cxx, cpp], success=False)
            assert not (root / name / 'model.h').exists()
        initialized = root / 'initialized.sv'
        initialized.write_text('module bad(input logic clk, output logic q=1); always_ff @(posedge clk) q<=~q; endmodule')
        run([args.hdlcpp, '--native-graph', '--top', 'bad', '--output', root / 'initialized.cc', initialized], success=False)
        assert not (root / 'initialized.cc').exists()
        returning = root / 'returning.sv'
        returning.write_text('module bad(input logic a, output logic y); function automatic logic f(input logic v); if(v) return 1; return 0; endfunction assign y=f(a); endmodule')
        run([args.hdlcpp, '--native-graph', '--top', 'bad', '--output', root / 'returning.cc', returning], success=False)
        assert not (root / 'returning.cc').exists()
        escaped = root / 'escaped.sv'
        escaped.write_text('module odd(input logic a, output logic y); logic \\internal)cpphdl_graph ; assign \\internal)cpphdl_graph = a; assign y = \\internal)cpphdl_graph ; endmodule')
        escaped_cpp = root / 'escaped.cc'
        run([args.hdlcpp, '--native-graph', '--top', 'odd', '--output', escaped_cpp, escaped])
        escaped_runner = root / 'escaped-run.cc'
        escaped_runner.write_text('#include "model.h"\nint main(){cpphdl_native::Model model; model.a[0]=1; model.step(); return model.y[0]!=1;}\n')
        run([args.cpphdl, '--native-graph', '--output', root / 'escaped', '--cxx', args.cxx,
             '--runner', escaped_runner, escaped_cpp])
        run([root / 'escaped/run'])
        bad_port = root / 'bad-port.sv'
        bad_port.write_text('module odd(input logic a, output logic \\y);bad ); assign \\y);bad = a; endmodule')
        bad_port_cpp = root / 'bad-port.cc'
        run([args.hdlcpp, '--native-graph', '--top', 'odd', '--output', bad_port_cpp, bad_port])
        run([args.cpphdl, '--native-graph', '--output', root / 'bad-port', '--cxx', args.cxx, bad_port_cpp], success=False)
        print('native graph: incomplete writes, cycles, unsupported initialization and overwrite checks passed')


if __name__ == '__main__':
    main()
