"""Serialized single-lane graphs preserve ordered DPI effects across chunks."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
from Cva6Adapter import write_dpi_adapter_header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    repo = fixtures.parents[1]
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='chunked-host-', dir=args.work) as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)
        write_dpi_adapter_header(work / 'dpi')

        def run(command):
            result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env,
                                    text=True, capture_output=True, timeout=240)
            if result.returncode:
                logs = ''.join(path.read_text(errors='replace')[-5000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            return result.stdout

        for name, macro in (('ModuleArrayPorts', 'MODULE_ARRAY_PORTS'),
                            ('PackedProxyAssignment', 'PACKED_PROXY_ASSIGNMENT'), ('Random', 'RANDOM'),
                            ('Jtag', 'JTAG'), ('Debug', 'DEBUG')):
            source = fixtures / ('CppGraph' + name + '.cc')
            output = work / name
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=1',
                 '--cxx', args.cxx, '--frontend-flag=-I' + str(work / 'dpi'),
                 '--runner', source, '--output', output, source, '--',
                 '-DCPP_GRAPH_' + macro + '_RUN', '-I' + str(repo / 'include'),
                 '-I' + str(work / 'dpi'), '-Wl,--wrap=random', '-fsanitize=address,undefined'])
            assert (output / 'model.graph').is_file()
            assert (output / 'memories.tsv').is_file()
            assert not (output / 'graph.cc').exists()
            print(run([output / 'run']), end='', flush=True)
        print('serialized chunked host callbacks: ordered effects and output projections PASS')


if __name__ == '__main__':
    main()
