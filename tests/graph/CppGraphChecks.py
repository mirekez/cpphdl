#!/usr/bin/env python3
import argparse
import importlib.util
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
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphDependencies.cc', '--output', work / 'dependencies',
             fixtures / 'CppGraphDependencies.cc', '--', '-DCPP_GRAPH_DEPENDENCIES_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'dependencies/run']).stdout, end='')
        shutil.rmtree(work / 'dependencies')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphConstants.cc', '--output', work / 'constants',
             fixtures / 'CppGraphConstants.cc', '--', '-DCPP_GRAPH_CONSTANTS_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'constants/run']).stdout, end='')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphDepth.cc', '--output', work / 'depth',
             fixtures / 'CppGraphDepth.cc', '--', '-DCPP_GRAPH_DEPTH_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'depth/run']).stdout, end='')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-DGRAPH_CALL_DEPTH=300', '--output', work / 'depth-limit',
             fixtures / 'CppGraphDepth.cc'], success=False)
        assert 'C++ call nesting limit' in (work / 'depth-limit/cpp-to-graph.log').read_text()
        assert not (work / 'depth-limit/model.h').exists()
        shutil.rmtree(work / 'depth')
        shutil.rmtree(work / 'depth-limit')
        for width in (1, 7, 8, 9, 16, 24, 32, 64, 65, 128):
            destination = work / ('byteswap-' + str(width))
            define = '-DGRAPH_BYTESWAP_WIDTH=' + str(width)
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=' + define, '--runner', fixtures / 'CppGraphByteswap.cc',
                 '--output', destination, fixtures / 'CppGraphByteswap.cc', '--',
                 '-DCPP_GRAPH_BYTESWAP_RUN', define,
                 '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
            print(run([destination / 'run']).stdout, end='')
            # Sanitized runners are large; completed width cases need no artifacts.
            shutil.rmtree(destination)
        for width in (8, 32, 64):
            destination = work / ('ranges-' + str(width))
            define = '-DGRAPH_RANGE_WIDTH=' + str(width)
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=' + define, '--runner', fixtures / 'CppGraphRanges.cc',
                 '--output', destination, fixtures / 'CppGraphRanges.cc', '--',
                 '-DCPP_GRAPH_RANGES_RUN', define,
                 '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
            print(run([destination / 'run']).stdout, end='')
        source = work / 'wide-range.cc'
        source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                          '_PORT(cpphdl::logic<1>) index_in; cpphdl::reg<cpphdl::logic<65>> state; '
                          'void _work(bool) { state._next = 0; state._next.bits(uint64_t(index_in()),0) = 1; } '
                          'void _strobe() { state.strobe(); } }; Bad cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'wide-range', source], success=False)
        assert 'wide or nested dynamic C++ bit range unsupported' in (work / 'wide-range/cpp-to-graph.log').read_text()
        assert not (work / 'wide-range/model.h').exists()
        for native_packed in (False, True):
            destination = work / ('packed-ranges-native' if native_packed else 'packed-ranges')
            defines = ['-DCPPHDL_NATIVE_PACKED'] if native_packed else []
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 *['--frontend-flag=' + define for define in defines],
                 '--runner', fixtures / 'CppGraphPackedRanges.cc', '--output', destination,
                 fixtures / 'CppGraphPackedRanges.cc', '--', '-DCPP_GRAPH_PACKED_RANGES_RUN',
                 *defines, '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
            print(run([destination / 'run']).stdout, end='')
            shutil.rmtree(destination)
        rtl = work / 'CppGraphPacked.sv'
        shutil.copyfile(fixtures / rtl.name, rtl)
        run([args.hdlcpp, rtl])
        rtl.unlink()
        header = (work / 'generated/CppGraphPacked.h').read_text()
        assert 'combined_comb.bits(' in header and 'retained._next.bits(' in header
        assert '.data.bits(' not in header
        source = work / 'packed-converted.cc'
        source.write_text('#include "generated/CppGraphPacked.h"\nCppGraphPacked cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-I' + str(work), '--runner', fixtures / 'CppGraphPackedRun.cc',
             '--output', work / 'packed-converted', source, '--', '-fsanitize=address,undefined'])
        print(run([work / 'packed-converted/run']).stdout, end='')
        shutil.rmtree(work / 'packed-converted')
        for name, state_type, target, high, diagnostic in [
            ('packed-wide-range', 'cpphdl::array<2, cpphdl::logic<64>, true>',
             'state._next', 'uint64_t(index_in())', 'wide or nested dynamic C++ bit range unsupported'),
            ('packed-current-range', 'cpphdl::array<1, cpphdl::logic<64>, true>',
             'state', '63', 'direct current-state C++ mutation unsupported'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<6>) index_in; cpphdl::reg<' + state_type + '> state; '
                              'void _work(bool) { state._next = 0; ' + target + '.bits(' + high + ',0) = 1; } '
                              'void _strobe() { state.strobe(); } }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            log = (work / name / 'cpp-to-graph.log').read_text()
            assert diagnostic in log, (name, log)
            assert not (work / name / 'model.h').exists()
        for words in (1, 2):
            destination = work / ('memory-' + str(words))
            define = '-DGRAPH_MEMORY_WORDS=' + str(words)
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=' + define, '--runner', fixtures / 'CppGraphMemory.cc',
                 '--output', destination, fixtures / 'CppGraphMemory.cc', '--', define,
                 '-DCPP_GRAPH_MEMORY_RUN', '-I' + str(fixtures.parents[1] / 'include'),
                 '-fsanitize=address,undefined'])
            print(run([destination / 'run']).stdout, end='')
            shutil.rmtree(destination)
        graph_sizes = []
        for depth in (17, 33554432):
            destination = work / ('memory-depth-' + str(depth))
            define = '-DGRAPH_MEMORY_DEPTH=' + str(depth)
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=' + define, '--runner', fixtures / 'CppGraphMemoryLarge.cc',
                 '--output', destination, fixtures / 'CppGraphMemoryLarge.cc', '--', define,
                 '-DCPP_GRAPH_MEMORY_LARGE_RUN', '-I' + str(fixtures.parents[1] / 'include'),
                 '-fsanitize=address,undefined'])
            print(run([destination / 'run']).stdout, end='')
            graph_sizes.append((destination / 'graph.cc').stat().st_size)
            assert (destination / 'model.h').stat().st_size < 32000
            shutil.rmtree(destination)
        assert abs(graph_sizes[1] - graph_sizes[0]) < 100
        rtl = work / 'CppGraphMemory.sv'
        shutil.copyfile(fixtures / rtl.name, rtl)
        run([args.hdlcpp, rtl])
        rtl.unlink()
        header = (work / 'generated/CppGraphMemory.h').read_text()
        assert 'memory<logic<64>,1,16>' in header and 'sram.pending(' in header and 'sram.apply();' in header
        source = work / 'memory-converted.cc'
        source.write_text('#include "generated/CppGraphMemory.h"\nCppGraphMemory cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-I' + str(work), '--runner', fixtures / 'CppGraphMemoryRun.cc',
             '--output', work / 'memory-converted', source, '--', '-I' + str(work),
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'memory-converted/run']).stdout, end='')
        shutil.rmtree(work / 'memory-converted')
        for name, width, body, strobe, diagnostic in [
            ('memory-missing-apply', 64, 'storage[0] = 7;', '', 'C++ memory writes without strobe apply'),
            ('memory-work-apply', 64, 'storage.apply();', '', 'C++ memory apply requires the strobe phase'),
            ('memory-double-apply', 64, 'storage[0] = 7;', 'storage.apply(); storage.apply();', 'repeated C++ memory apply unsupported'),
            ('memory-padded', 9, 'storage[0] = 7;', 'storage.apply();', 'C++ memory requires unpadded logic elements'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              'cpphdl::memory<cpphdl::logic<' + str(width) + '>,1,16> storage; '
                              'void _work(bool) {' + body + '} void _strobe() {' + strobe + '} }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            log = (work / name / 'cpp-to-graph.log').read_text()
            assert diagnostic in log, (name, log)
            assert not (work / name / 'model.h').exists()
        for name, expression, diagnostic in [
            ('memory-output-write', 'storage[0] = 7', 'C++ memory write requires a work transaction'),
            ('memory-output-pending', 'storage.pending(0)', 'pending C++ memory read requires a work transaction'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                              'cpphdl::memory<cpphdl::logic<64>,1,16> storage; '
                              '_PORT(cpphdl::logic<64>) result_out = _ASSIGN(result()); '
                              'cpphdl::logic<64> result() {' + expression + '; return 0;} }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            log = (work / name / 'cpp-to-graph.log').read_text()
            assert diagnostic in log, (name, log)
            assert not (work / name / 'model.h').exists()
        source = work / 'memory-initialized.cc'
        source.write_text('#include "cpphdl.h"\nclass Bad: public cpphdl::Module { public: '
                          'cpphdl::memory<cpphdl::logic<64>,1,16> storage{}; '
                          '_PORT(cpphdl::logic<64>) result_out = _ASSIGN(cpphdl::logic<64>(storage[0])); }; Bad cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'memory-initialized', source], success=False)
        assert 'initialized C++ memory unsupported' in (work / 'memory-initialized/cpp-to-graph.log').read_text()
        assert not (work / 'memory-initialized/model.h').exists()
        for name, members, layout, size, diagnostic in [
            ('constant-overlap', 'unsigned char first, second;',
             'static constexpr unsigned __hdlcpp_offset_first=0, __hdlcpp_offset_second=0;', 8,
             'overlapping constant packed fields'),
            ('constant-bounds', 'unsigned char first;', 'static constexpr unsigned __hdlcpp_offset_first=1;', 8,
             'constant packed field out of bounds'),
            ('constant-gap', 'unsigned char first;', 'static constexpr unsigned __hdlcpp_offset_first=0;', 16,
             'incomplete constant packed layout'),
            ('constant-metadata', 'unsigned char first;', '', 8, 'missing packed field metadata'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\nstruct Packed {' + members + layout +
                              'static constexpr unsigned _size_bits() { return ' + str(size) + '; } '
                              'cpphdl::logic<' + str(size) + '> pack() const { return 0; }}; '
                              'inline constexpr Packed constant{}; '
                              'class Bad: public cpphdl::Module { public: _PORT(cpphdl::logic<' + str(size) +
                              '>) value_out = _ASSIGN(constant.pack()); }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            log = (work / name / 'cpp-to-graph.log').read_text()
            assert diagnostic in log, (name, log)
            assert not (work / name / 'model.h').exists()
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphExtern.cc', '--output', work / 'extern-root',
             fixtures / 'CppGraphExtern.cc', '--', '-DCPP_GRAPH_EXTERN_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-fsanitize=address,undefined'])
        print(run([work / 'extern-root/run']).stdout, end='')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixtures / 'CppGraphRandom.cc', '--output', work / 'random',
             fixtures / 'CppGraphRandom.cc', '--', '-DCPP_GRAPH_RANDOM_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-Wl,--wrap=random',
             '-fsanitize=address,undefined'])
        print(run([work / 'random/run']).stdout, end='')
        converter = fixtures.parents[1] / 'hdlcpp/tests/cva6/support/cpphdl/tools/convert_cva6.py'
        specification = importlib.util.spec_from_file_location('cva6_converter', converter)
        conversion = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(conversion)
        conversion.write_dpi_adapter_header(work / 'dpi')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-I' + str(work / 'dpi'),
             '--runner', fixtures / 'CppGraphJtag.cc', '--output', work / 'jtag',
             fixtures / 'CppGraphJtag.cc', '--', '-DCPP_GRAPH_JTAG_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-I' + str(work / 'dpi'),
             '-Wl,--wrap=random', '-fsanitize=address,undefined'])
        print(run([work / 'jtag/run']).stdout, end='')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--frontend-flag=-I' + str(work / 'dpi'),
             '--runner', fixtures / 'CppGraphDebug.cc', '--output', work / 'debug',
             fixtures / 'CppGraphDebug.cc', '--', '-DCPP_GRAPH_DEBUG_RUN',
             '-I' + str(fixtures.parents[1] / 'include'), '-I' + str(work / 'dpi'),
             '-Wl,--wrap=random', '-fsanitize=address,undefined'])
        print(run([work / 'debug/run']).stdout, end='')
        for name, call, diagnostic in [
            ('debug-alias', 'debug_tick(&valid, 0, &addr, &addr, &data, 0, &ready, 0, 0)',
             'aliased debug_tick output addresses unsupported'),
            ('debug-dynamic', 'debug_tick(&valid, 0, &words[index_in()], &op, &data, 0, &ready, 0, 0)',
             'dynamic debug_tick output address unsupported'),
            ('debug-output-call', 'debug_tick(&valid, 0, &addr, &op, &data, 0, &ready, 0, 0)',
             'debug_tick requires a C++ work transaction'),
            ('debug-abi', 'debug_tick(&valid, 0, &addr, &op, &data, 0, &ready, 0, 0)',
             'unsupported debug_tick C ABI'),
            ('mutable-lambda', '[value = int(uint64_t(index_in()))]() mutable { return ++value; }()',
             'mutable C++ lambda unsupported'),
        ]:
            source = work / (name + '.cc')
            result_type = 'long' if name == 'debug-abi' else 'int'
            member = ('int comb() { return ' + call + '; }' if name == 'debug-output-call'
                      else 'void _work(bool) { (void)' + call + '; }')
            observed = 'comb()' if name == 'debug-output-call' else 'addr'
            source.write_text('#include "cpphdl.h"\nextern "C" ' + result_type + ' debug_tick('
                              'unsigned char*, unsigned char, int*, int*, int*, unsigned char, unsigned char*, int, int);\n'
                              'class Bad: public cpphdl::Module { public: '
                              'unsigned char valid, ready; int addr, op, data; cpphdl::array<2, int> words; '
                              '_PORT(cpphdl::logic<1>) index_in; '
                              '_PORT(cpphdl::logic<32>) result_out = _ASSIGN(' + observed + '); '
                              + member + ' }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            log = (work / name / 'cpp-to-graph.log').read_text()
            assert diagnostic in log, (name, log)
            assert not (work / name / 'model.h').exists()
        for name, members, diagnostic in [
            ('jtag-alias', 'unsigned char other; void _work(bool) { '
             '(void)jtag_tick(&pin, &pin, &other, &other, 0); }',
             'aliased jtag_tick output addresses unsupported'),
            ('work-initializer', 'void _work(bool) { pin=0; }',
             'initialized C++ work storage unsupported'),
            ('work-mixed-writer', 'unsigned char& comb() { pin=3; return pin; } '
             'void _work(bool) { pin=4; }', 'mixed work/combinational C++ writes'),
            ('jtag-output-call', 'unsigned char other; unsigned char& comb() { '
             '(void)jtag_tick(&pin, &pin, &other, &other, 0); return pin; }',
             'jtag_tick requires a C++ work transaction'),
        ]:
            source = work / (name + '.cc')
            observed = 'comb()' if 'comb()' in members else 'pin'
            initializer = '=7' if name == 'work-initializer' else ''
            source.write_text('#include "cpphdl.h"\nextern "C" int jtag_tick(unsigned char*, '
                              'unsigned char*, unsigned char*, unsigned char*, unsigned char);\n'
                              'class Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<8>) result_out = _ASSIGN_REG(' + observed + '); '
                              'unsigned char pin' + initializer + '; ' + members + ' }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            assert diagnostic in (work / name / 'cpp-to-graph.log').read_text()
            assert not (work / name / 'model.h').exists()
        source = work / 'jtag-abi.cc'
        source.write_text('#include "cpphdl.h"\nextern "C" int jtag_tick(unsigned*, unsigned*, unsigned*, unsigned*, unsigned);\n'
                          'class Bad: public cpphdl::Module { public: unsigned pin; '
                          '_PORT(cpphdl::logic<32>) result_out = _ASSIGN_REG(pin); '
                          'void _work(bool) { (void)jtag_tick(&pin,&pin,&pin,&pin,0); } }; Bad cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'jtag-abi', source], success=False)
        assert 'unsupported jtag_tick C ABI' in (work / 'jtag-abi/cpp-to-graph.log').read_text()
        for name, members, diagnostic in [
            ('random-output', 'cpphdl::logic<8>& comb() { stored=::random(); return stored; }',
             'random() requires a C++ work transaction'),
            ('random-getter-reuse', 'cpphdl::logic<8>& comb() { return stored; } '
             'cpphdl::logic<8>& producer() { stored=::random(); return stored; } '
             'void _work(bool) { (void)producer(); (void)producer(); }',
             'repeated effectful C++ getter unsupported'),
            ('random-output-alias', 'cpphdl::logic<8>& comb() { return stored; } '
             'static constexpr bool __cpphdl_net_stored=true; '
             'cpphdl::logic<8>& stored_func() { stored=::random(); return stored; } '
             'void _work(bool) { (void)stored_func(); }',
             'transactional host effect reaches a combinational output'),
        ]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\n#include <cstdlib>\n'
                              'class Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<8>) value_out = _ASSIGN_REG(comb()); '
                              'cpphdl::logic<8> stored; ' + members + ' }; Bad cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            assert diagnostic in '\n'.join(path.read_text() for path in (work / name).glob('*.log'))
            assert not (work / name / 'model.h').exists()
        source = work / 'incomplete.cc'
        source.write_text('class Incomplete; extern Incomplete cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'incomplete', source], success=False)
        assert 'incomplete C++ graph root type' in (work / 'incomplete/cpp-to-graph.log').read_text()
        source = work / 'foreign.cc'
        source.write_text('#include "cpphdl.h"\nextern unsigned external_tick(unsigned);\n'
                          'class Foreign: public cpphdl::Module { public: '
                          '_PORT(cpphdl::logic<8>) data_in; '
                          '_PORT(cpphdl::logic<8>) result_out = _ASSIGN_REG(state); '
                          'cpphdl::reg<cpphdl::logic<8>> state; '
                          'void _work(bool reset) { state._next=reset ? 0 : external_tick(uint64_t(data_in())); } '
                          'void _strobe() { state.strobe(); } }; Foreign cpphdl_top;\n')
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'foreign', source], success=False)
        assert 'missing instantiated C++ body: external_tick' in (work / 'foreign/cpp-to-graph.log').read_text()
        assert not (work / 'foreign/model.h').exists()
        for name, constructor in [('template-constructor', 'Bad() { state=7; }'),
                                  ('template-initializer', 'Bad() : state(7) {}')]:
            source = work / (name + '.cc')
            source.write_text('#include "cpphdl.h"\ntemplate<unsigned Width> '
                              'class Bad: public cpphdl::Module { public: '
                              '_PORT(cpphdl::logic<Width>) output_out = _ASSIGN_REG(state); '
                              'cpphdl::reg<cpphdl::logic<Width>> state; ' + constructor +
                              ' }; extern Bad<8> cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--output', work / name, source], success=False)
            diagnostic = (work / name / 'cpp-to-graph.log').read_text()
            assert 'explicit module initialization' in diagnostic or 'nonempty module constructor' in diagnostic
        for name, members in [
            ('latch', 'cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { if (a_in()) stored=7; return stored; }'),
            ('constructor', 'Bad() { stored=7; } cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { return stored; }'),
            ('negative_phase', 'void _work_neg(bool) {} void _strobe_neg() { stored=0; } cpphdl::logic<8> stored; cpphdl::logic<8>& comb() { stored=0; return stored; }'),
            ('side_effect', 'cpphdl::logic<8> stored, another; cpphdl::logic<8>& comb() { stored=0; another=7; return stored; }'),
            ('current_state', 'cpphdl::reg<cpphdl::logic<8>> stored; cpphdl::reg<cpphdl::logic<8>>& comb() { stored=7; return stored; }'),
            ('current_slice', 'cpphdl::reg<cpphdl::logic<8>> stored; cpphdl::reg<cpphdl::logic<8>>& comb() { stored.bits(3,1)=7; return stored; }'),
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
