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
    with tempfile.TemporaryDirectory(prefix='constant-rom-', dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(tools.command(command), cwd=work, env=tools.env,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        rtl, runner = fixtures / 'ConstPackedRom.sv', fixtures / 'ConstPackedRomRun.cc'
        flags = ['-std=c++23', '-O1', '-I' + str(fixtures.parents[1] / 'include'),
                 '-I' + str(work)]
        run([args.hdlcpp, rtl])
        run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
        run([work / 'ordinary'])
        output = work / 'verilator'
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'ConstPackedRom', '--Mdir', output,
             '-CFLAGS', ' '.join(flags + ['-DROM_RTL']),
             '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
        run([output / 'VConstPackedRom'])
        seed = work / 'seed.cc'
        seed.write_text('#include "generated/ConstPackedRom.h"\nextern ConstPackedRom cpphdl_top;\n')
        for chunk in (0, 2):
            output = work / ('graph-' + str(chunk))
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=' + str(chunk),
                 '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                 '--runner', runner, seed, '--', *flags, '-DROM_GRAPH',
                 '-fsanitize=address,undefined'])
            run([output / 'run'])
        assert 'const array<' in (work / 'generated/ConstPackedRom.h').read_text()
        for name, member, diagnostic in (
            ('mutable', 'cpphdl::logic<8> stored = 7;', 'undriven bit'),
            ('input', 'const cpphdl::logic<8> stored = input_in();',
             'nonconstant C++ constant initializer'),
            ('recursive', 'const cpphdl::logic<8> stored = stored;',
             'cyclic C++ constant initializer'),
        ):
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad : public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<8>) input_in; ' + member +
                              ' _PORT(cpphdl::logic<8>) result_out = _ASSIGN(stored); '
                              'void _assign() {} void _work(bool) {} void _strobe() {} }; '
                              'extern Bad cpphdl_top;\n')
            output = work / name
            result = subprocess.run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top',
                                     '--cxx', args.cxx, '--output', str(output), str(source)],
                                    cwd=work, capture_output=True, text=True, timeout=180)
            logs = ''.join(path.read_text() for path in output.glob('*.log'))
            assert result.returncode and diagnostic in logs, (name, result.stderr, logs)
            print(name + ' initializer rejected', flush=True)


if __name__ == '__main__':
    main()
