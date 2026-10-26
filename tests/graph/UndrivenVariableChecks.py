import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    for name in ('cpphdl', 'hdlcpp', 'cxx', 'verilator'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    with tempfile.TemporaryDirectory(prefix='undriven-variable-', dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command, environment=None):
            result = subprocess.run(tools.command(command), cwd=work,
                                    env=tools.env if environment is None else environment,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            return result.stdout

        rtl = work / 'UndrivenVariable.sv'
        shutil.copyfile(fixture / rtl.name, rtl)
        runner = fixture / 'UndrivenVariableRun.cc'
        for enabled in (0, 1):
            configured = work / 'generate.tsv'
            configured.write_text(f'UndrivenVariable.ENABLE\t{enabled}\n')
            environment = tools.env.copy()
            environment['HDLCPP_GENERATE_PARAM_VALUES'] = str(configured)
            run([args.hdlcpp, rtl], environment)
            header = (work / 'generated/UndrivenVariable.h').read_text()
            assert 'untouched = logic<' in header
            assert ('optional_q = logic<' in header) == (enabled == 0)
            assert 'child_data = logic<' not in header
            assert 'written = logic<' not in header
            assert 'external_value = logic<' not in header
            assert 'external_bank = ' not in header
            assert 'empty_array = array<' in header
            assert 'empty_struct = config_t{};' in header
            assert 'driven = config_t{};' not in header
            assert 'unused_backing = ' not in header
            assert 'initial_hook = ' not in header
            flags = ['-std=c++23', '-O1', '-I' + str(include), '-I' + str(work),
                     '-DENABLE_VALUE=' + str(enabled)]
            run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
            print(run([work / 'ordinary']), end='', flush=True)
            seed = work / 'seed.cc'
            seed.write_text(f'''#include "cpphdl.h"
#define private public
#include "generated/UndrivenVariable.h"
#undef private
class Wrapper : public cpphdl::Module {{
public:
    _PORT(cpphdl::logic<8>) data_i_in;
    _PORT(cpphdl::logic<1>) rst_ni_in;
    _PORT(cpphdl::logic<32>) result_o_out = _ASSIGN(dut.result_o_out());
    _PORT(cpphdl::logic<32>) aggregate_o_out = _ASSIGN(dut.aggregate_o_out());
    UndrivenVariable<{enabled}> dut;
    void _assign() {{ dut.data_i_in = _ASSIGN(data_i_in()); dut.rst_ni = _ASSIGN(rst_ni_in()); dut._assign(); }}
    void _work(bool reset) {{ dut.initial_hook = data_i_in(); dut._work(reset); }}
    void _strobe() {{ dut._strobe(); }}
}};
extern Wrapper cpphdl_top;
''')
            output = work / ('graph-' + str(enabled))
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work), '--chunk-size=2', '--runner', runner,
                 '--output', output, seed, '--', *flags, '-DUNDRIVEN_GRAPH', '-fsanitize=address,undefined'])
            print(run([output / 'run']), end='', flush=True)
            output = work / ('verilator-' + str(enabled))
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'UndrivenVariable', '-GENABLE=' + str(enabled), '--Mdir', output,
                 '-CFLAGS', '-std=c++23 -O2 -DUNDRIVEN_VERILATOR -DENABLE_VALUE=' + str(enabled),
                 '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
            print(run([output / 'VUndrivenVariable']), end='', flush=True)
        reference_rtl = work / 'ReferenceWrites.sv'
        reference_rtl.write_text('''import "DPI-C" function int reference_tick(output int value);
module ReferenceWrites(input logic clk_i, output logic [31:0] data_o);
  int callback_data, status;
  always @(posedge clk_i) status = reference_tick(callback_data) + 1;
  assign data_o = callback_data;
endmodule
''')
        run([args.hdlcpp, reference_rtl])
        header = (work / 'generated/ReferenceWrites.h').read_text()
        assign_body = header.split('void _assign()', 1)[1]
        assert not any('callback_data =' in line for line in assign_body.splitlines())
        assert 'callback_data = callback_data;' in header
        print('nested DPI output driver classification: PASS', flush=True)


if __name__ == '__main__':
    main()
