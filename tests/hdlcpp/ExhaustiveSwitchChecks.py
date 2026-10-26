import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    for name in ('hdlcpp', 'cpphdl', 'cxx', 'verilator'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='exhaustive-switch-', dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command, success=True):
            result = subprocess.run(tools.command(command), cwd=work, env=tools.env,
                                    capture_output=True, text=True, timeout=180)
            if (result.returncode == 0) != success:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        rtl, runner = fixtures / 'ExhaustiveSwitch.sv', fixtures / 'ExhaustiveSwitchRun.cc'
        flags = ['-std=c++23', '-O1', '-I' + str(fixtures.parents[1] / 'include'),
                 '-I' + str(work)]
        run([args.hdlcpp, rtl])
        run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
        run([work / 'ordinary'])
        seed = work / 'seed.cc'
        seed.write_text('#include "generated/ExhaustiveSwitch.h"\nextern ExhaustiveSwitch cpphdl_top;\n')
        for chunk in (0, 2):
            output = work / ('graph-' + str(chunk))
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=' + str(chunk),
                 '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                 '--runner', runner, seed, '--', *flags, '-DSWITCH_GRAPH',
                 '-fsanitize=address,undefined'])
            run([output / 'run'])
        output = work / 'verilator'
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'ExhaustiveSwitch', '--Mdir', output,
             '-CFLAGS', ' '.join(flags + ['-DSWITCH_RTL']),
             '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
        run([output / 'VExhaustiveSwitch'])
        for name, cases in (
            ('missing-case', 'case 0: case 1: case 2: stored=data_in(); break;'),
            ('missing-write', 'case 0: case 1: case 2: stored=data_in(); break; case 3: break;'),
        ):
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<2>) selector_in; _PORT(cpphdl::logic<8>) data_in; '
                              'cpphdl::logic<8> stored; _PORT(cpphdl::logic<8>) result_out=_ASSIGN(comb()); '
                              'cpphdl::logic<8>& comb() { switch(uint64_t(selector_in())) {' + cases +
                              '} return stored; } }; extern Bad cpphdl_top;\n')
            output = work / name
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=2', '--cxx', args.cxx,
                 '--output', output, source], success=False)
            diagnostic = (output / 'cpp-to-graph.log').read_text()
            assert 'undriven bit' in diagnostic, (name, diagnostic)
            assert not (output / 'model.h').exists()
        print('incomplete switch coverage/writes remain rejected: PASS', flush=True)


if __name__ == '__main__':
    main()
