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
    with tempfile.TemporaryDirectory(prefix='projection-carrier-', dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)
        environment = tools.env.copy()
        environment['HDLCPP_APPEND_FINAL_MODULE_TRAITS'] = str(work / 'final.tsv')
        traits = work / 'traits.tsv'
        traits.write_text('ProjectionCarrierChild\toutput_field.req_o.ar_valid\n')
        environment['HDLCPP_MODULE_TRAITS'] = str(traits)

        def run(command):
            result = subprocess.run(tools.command(command), cwd=work, env=environment,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        rtl = fixtures / 'ProjectionCarrier.sv'
        runner = fixtures / 'ProjectionCarrierRun.cc'
        run([args.hdlcpp, rtl])
        assert 'ar_valid.ar' not in (work / 'final.tsv').read_text()
        assert '.ar_valid.ar' not in (work / 'generated/ProjectionCarrier.h').read_text()
        flags = ['-std=c++23', '-O1', '-I' + str(fixtures.parents[1] / 'include'), '-I' + str(work)]
        parent = (work / 'generated/ProjectionCarrier.h').read_text().split(
            'class ProjectionCarrier : public Module', 1)[1]
        if 'req_i_in__field_ar_valid;' in parent:
            flags.append('-DCARRIER_VALID_BIND')
        if 'req_i_in__field_ar;' in parent:
            flags.append('-DCARRIER_AR_BIND')
        run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
        run([work / 'ordinary'])
        seed = work / 'seed.cc'
        seed.write_text('''#include "generated/ProjectionCarrier.h"
class Wrapper : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<9>) req_i_in;
    ProjectionCarrier<> dut;
    _PORT(cpphdl::logic<8>) value_o_out = _ASSIGN(dut.value_o_out());
    void _assign() {
        dut.req_i_in = _ASSIGN(cpphdl::convert_packed<ProjectionCarrierTypes::req_t>(req_i_in()));
#ifdef CARRIER_AR_BIND
        dut.req_i_in__field_ar = _ASSIGN(cpphdl::logic<8>(req_i_in()));
#endif
#ifdef CARRIER_VALID_BIND
        dut.req_i_in__field_ar_valid = _ASSIGN(cpphdl::logic<1>(uint64_t(req_i_in()) >> 8));
#endif
        dut._assign();
    }
    void _work(bool reset) { dut._work(reset); }
    void _strobe() { dut._strobe(); }
};
extern Wrapper cpphdl_top;
''')
        output = work / 'graph'
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=2',
             '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
             '--runner', runner, seed, '--', *flags, '-DCARRIER_GRAPH', '-fsanitize=address,undefined'])
        run([output / 'run'])
        output = work / 'verilator'
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'ProjectionCarrier', '--Mdir', output,
             '-CFLAGS', ' '.join(flags + ['-DCARRIER_RTL']), '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
        run([output / 'VProjectionCarrier'])


if __name__ == '__main__':
    main()
