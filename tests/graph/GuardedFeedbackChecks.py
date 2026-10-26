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
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--verilator', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    with tempfile.TemporaryDirectory(prefix='guarded-feedback-', dir=args.work) as directory:
        work = Path(directory)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command, success=True):
            result = subprocess.run(tools.command(command), cwd=work, env=tools.env,
                                    capture_output=True, text=True, timeout=180)
            if (result.returncode == 0) != success:
                logs = '\n'.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.returncode, result.stdout, result.stderr, logs))
            return result

        runner = fixture / 'GuardedFeedback.cc'
        sharing = work / 'sharing'
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined', '-I' + str(include),
             fixture / 'GuardedFeedbackSharing.cc', '-o', sharing])
        run([sharing])
        flags = ['-DGRAPH_GUARDED_FEEDBACK_RUN', '-I' + str(include), '-fsanitize=address,undefined']
        cases = [('GuardedFeedback', []), ('GuardedSelector', ['-DGRAPH_GUARDED_SELECTOR']),
                 ('GuardedCoupled', ['-DGRAPH_GUARDED_COUPLED'])]
        for name, defines in cases:
            for chunk in (0, 2):
                output = work / (name + '-cpp-' + str(chunk))
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                     *['--frontend-flag=' + define for define in defines],
                     '--chunk-size=' + str(chunk), '--runner', runner, '--output', output, runner,
                     '--', *flags, *defines])
                print(run([output / 'run']).stdout, end='', flush=True)
        output = work / 'true-cycle'
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-DGRAPH_TRUE_FEEDBACK', '--output', output, runner], success=False)
        assert any('combinational cycle' in path.read_text() for path in output.glob('*.log'))

        rtl = work / 'GuardedFeedback.sv'
        shutil.copyfile(fixture / rtl.name, rtl)
        run([args.hdlcpp, rtl])
        for name, defines in cases:
            seed = work / (name + '-seed.cc')
            seed.write_text(f'#include "generated/GuardedFeedback.h"\nextern {name} cpphdl_top;\n')
            output = work / (name + '-sv-cpp-graph')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work), '--chunk-size=2', '--runner', runner,
                 '--output', output, seed, '--', *flags, '-DGRAPH_GUARDED_FEEDBACK_SV', *defines])
            print(run([output / 'run']).stdout, end='', flush=True)
            output = work / (name + '-verilator')
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal', '--top-module',
                 name, '--Mdir', output, '-CFLAGS',
                 '-std=c++23 -O2 -DGRAPH_GUARDED_FEEDBACK_RUN -DGRAPH_GUARDED_FEEDBACK_VERILATOR '
                 + ' '.join(defines) + ' -I' + str(include),
                 '-MAKEFLAGS', 'CXX=' + args.cxx, fixture / 'GuardedFeedback.sv', runner])
            print(run([output / ('V' + name)]).stdout, end='', flush=True)
        print('guarded feedback: C++, SV-to-C++ graph, and original Verilator agree; true loops rejected', flush=True)


if __name__ == '__main__':
    main()
