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
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cpp-graph-', dir=args.work) as directory:
        work = Path(directory)

        def run(command, success=True):
            result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True,
                                    text=True, timeout=180)
            if (result.returncode == 0) != success:
                logs = '\n'.join(str(path) + '\n' + path.read_text()[-5000:]
                                 for path in work.rglob('*.log'))
                raise AssertionError((command, result.returncode, result.stdout, result.stderr, logs))
            return result

        rtl = work / 'CppGraph.sv'
        shutil.copyfile(fixtures / rtl.name, rtl)
        run([args.hdlcpp, rtl])
        rtl.unlink()
        shutil.copyfile(fixtures / 'CppGraphSeed.cc', work / 'model.cc')
        command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top',
                   '--frontend-flag=-I' + str(work), '--cxx', args.cxx,
                   '--runner', fixtures / 'CppGraphRun.cc', work / 'model.cc']
        run([*command, '--output', work / 'baseline', '--', '-fsanitize=address,undefined'])
        print(run([work / 'baseline/run']).stdout, end='')
        before = (work / 'baseline/model.h').read_bytes()
        run([*command, '--output', work / 'baseline'], success=False)
        assert (work / 'baseline/model.h').read_bytes() == before
        header = work / 'generated/CppGraph.h'
        original = header.read_text()
        assert '^' in original
        header.write_text(original.replace(' ^ ', ' | '))
        run([*command, '--output', work / 'mutated'])
        run([work / 'mutated/run'], success=False)
        print('ordinary C++ graph: C++ mutation changes behavior; RTL absent; overwrite rejected')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphFeatures.cc', '--output', work / 'features',
             fixtures / 'CppGraphFeatures.cc', '--', '-DCPP_GRAPH_FEATURES_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'features/run']).stdout, end='')
        for name, members in [
            ('latch', 'cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { if (a_in()) stored=7; return stored; }'),
            ('constructor', 'Bad() { stored=7; } cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { return stored; }'),
            ('negative_phase', 'void _work_neg(bool) {} void _strobe_neg() { stored=0; } cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { stored=0; return stored; }'),
            ('side_effect', 'cpphdl::logic<8> stored, another; cpphdl::logic<8>& comb() { stored=0; another=7; return stored; }'),
            ('current_state', 'cpphdl::reg<cpphdl::logic<8>> stored; cpphdl::reg<cpphdl::logic<8>>& comb() { stored=7; return stored; }'),
            ('conditional_break', 'cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { stored=0; switch(uint64_t(a_in())) { case 0: { if(a_in()) break; stored=7; break; } default: { stored=9; break; } } return stored; }'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<1>) a_in; '
                              '_PORT(cpphdl::logic<8>) y_out = _ASSIGN_REG(comb()); '
                              + members + ' }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            assert not (work / name / 'model.h').exists()
        print('ordinary C++ graph: incomplete writes and unsupported lifecycle rejected')
        rtl = work / 'CppGraphNets.sv'
        shutil.copyfile(fixtures / rtl.name, rtl)
        run([args.hdlcpp, rtl])
        rtl.unlink()
        source = work / 'nets.cc'
        source.write_text('#include "generated/CppGraphNets.h"\nCppGraphNets cpphdl_top;\n')
        command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                   '--runner', fixtures / 'CppGraphNetsRun.cc', source]
        run([*command, '--output', work / 'nets'])
        print(run([work / 'nets/run']).stdout, end='')
        header = work / 'generated/CppGraphNets.h'
        original = header.read_text()
        assert '__cpphdl_net_tree_comb = true' in original
        header.write_text(original.replace('__cpphdl_net_tree_comb = true', '__cpphdl_net_tree_comb = false'))
        run([*command, '--output', work / 'unmarked-nets'], success=False)
        assert not (work / 'unmarked-nets/model.h').exists()
        for name, body in [('multiple_drivers', 'data_comb[0]=a_in(); data_comb[0]=~a_in();'),
                           ('net_cycle', 'data_comb=~data_comb;'),
                           ('undriven_net', 'data_comb[0]=a_in();')]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<1>) a_in; '
                              '_PORT(cpphdl::logic<8>) y_out = _ASSIGN_REG(data_comb_func()); '
                              'cpphdl::logic<8> data_comb; static constexpr bool __cpphdl_net_data_comb=true; '
                              'cpphdl::logic<8>& data_comb_func() { ' + body + ' return data_comb; } }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            assert not (work / name / 'model.h').exists()
        print('ordinary C++ graph: net metadata never bypasses driver/cycle checks')


if __name__ == '__main__':
    main()
