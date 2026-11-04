"""Convert an RTL example and run its inline testbench against Verilator."""
import argparse
from pathlib import Path
import subprocess
import shlex
import shutil
import json
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser()
parser.add_argument('--cpphdl', required=True)
parser.add_argument('--cxx', required=True)
parser.add_argument('--verilator', required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--top', required=True)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--define', action='append', default=[], help='Definition shared by conversion and test compilation')
parser.add_argument('--self-test', type=Path,
                    help='Run a legacy example executable that builds its own Verilator parameter matrix')
args = parser.parse_args()
args.work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix=args.top + '-', dir=args.work) as directory:
    work = Path(directory)
    toolchain = TestToolchain(args.cxx, args.verilator, work)

    def run(command):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(f'Command failed ({result.returncode}): {command}')
        return result.stdout

    generated = work / 'generated'
    options = []
    if args.self_test:
        if args.source.stem in ('FpConverter', 'AvalonEndpoint',
                                'Axi4MuxFromSlave', 'Axi4MuxToMaster'):
            options += ['--no-synthesis-flag']
        if args.source.stem == 'FpAdd':
            options += ['--primary_clock', 'sum_clock', '100000000']
        if args.source.stem == 'Fifo2clk':
            options += ['--primary_clock', 'write_clk', '100000000',
                        '--secondary_clock', 'read_clk', '50000000']
    run([args.cpphdl, *options, '--generated-dir=' + str(generated), args.source.resolve(),
         '--', '-DMAINFILE', *['-D' + value for value in args.define], '-I' + str(ROOT / 'include')])
    if args.self_test:
        if args.source.stem == 'RiscV':
            for name in ('rv32i.bin', 'rv32i.log'):
                shutil.copyfile(ROOT / 'examples/tuple' / name, work / name)
        toolchain.env['CPPHDL_VERILATOR'] = str(Path(args.verilator).resolve())
        toolchain.env['CPPHDL_VERILATOR_CXX'] = args.cxx
        print(run([args.self_test.resolve()]), end='')
        sys.exit(0)
    sources = sorted(generated.glob('*.sv'), key=lambda p: (p.name != 'Predef_pkg.sv',
                     not p.name.endswith('_pkg.sv'), p.name))
    if not (generated / (args.top + '.sv')).exists():
        raise RuntimeError('Converter did not emit the requested top module')
    driver = work / 'example_main.cpp'
    driver.write_text('#include ' + json.dumps(str(args.source.resolve())) + '\n' +
                      '#include ' + json.dumps(str(ROOT / 'tests/ExampleMain.cpp')) + '\n')
    output = run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
         '--top-module', args.top, '--Mdir', work / 'obj',
         '-CFLAGS', shlex.join(['-std=c++23', '-ffp-contract=off', '-DVERILATOR', '-DMAINFILE',
                               '-I' + str(ROOT / 'include'), *['-D' + value for value in args.define]]),
         *sources, driver])
    if any(warning in output for warning in ('%Warning-LATCH', '%Warning-MULTIDRIVEN', '%Warning-UNOPTFLAT')):
        raise RuntimeError('Unexpected hardware warning:\n' + output)
    print(run([work / 'obj' / ('V' + args.top)]), end='')
