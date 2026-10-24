import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    for name in ('cpphdl', 'cxx'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='native-split-', dir=args.work) as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)
        converter = fixtures.parents[1] / 'hdlcpp/tests/cva6/support/cpphdl/tools/convert_cva6.py'
        specification = importlib.util.spec_from_file_location('split_converter', converter)
        conversion = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(conversion)
        conversion.write_dpi_adapter_header(work / 'dpi')
        for name, macro in (('Features', 'CPP_GRAPH_FEATURES_RUN'),
                            ('Memory', 'CPP_GRAPH_MEMORY_RUN'),
                            ('Jtag', 'CPP_GRAPH_JTAG_RUN'),
                            ('Debug', 'CPP_GRAPH_DEBUG_RUN')):
            fixture = fixtures / ('CppGraph' + name + '.cc')
            output = work / name
            flags = ['-D' + macro, '-I' + str(fixtures.parents[1] / 'include'),
                     '-I' + str(work / 'dpi'), '-fsanitize=address,undefined']
            if name in ('Jtag', 'Debug'):
                flags.append('-Wl,--wrap=random')
            command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                       '--chunk-size=16', '--split-chunks=4', '--frontend-flag=-I' + str(work / 'dpi'),
                       '--runner', fixture, '--output', output, fixture, '--', *flags]
            result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env,
                                    capture_output=True, text=True, timeout=300)
            if result.returncode:
                logs = ''.join(path.read_text()[-4000:] for path in output.glob('*.log'))
                raise AssertionError((name, result.stdout, result.stderr, logs))
            assert list(output.glob('model.part.*.cpp'))
            result = subprocess.run([str(output / 'run')], cwd=work, env=toolchain.env, capture_output=True,
                                    text=True, timeout=180)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            print(result.stdout, end='', flush=True)
        print('split native compilation: state, memory, JTAG/debug effects and settling PASS', flush=True)


if __name__ == '__main__':
    main()
