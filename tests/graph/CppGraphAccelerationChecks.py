#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--memory-limit-mib', type=int, default=768)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    include = Path(__file__).resolve().parents[2] / 'include'

    def limit_memory():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        limit = args.memory_limit_mib * 1024**2
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))

    def lower(source):
        print(f'Lowering {source.name}', flush=True)
        result = subprocess.run([str(args.cpphdl.resolve()), '--lower-cpp-graph', str(source),
                                 str(source.with_suffix('.graph.cc')), 'cpphdl_top', '--',
                                 '-std=c++23', '-I' + str(include)],
                                capture_output=True, text=True, timeout=120,
                                preexec_fn=limit_memory)
        if result.returncode:
            raise AssertionError((result.returncode, result.stderr[-5000:]))
        return result.stderr

    with tempfile.TemporaryDirectory(prefix='cpp-graph-memory-', dir=args.work) as directory:
        work = Path(directory)
        for stages in (80, 160):
            fields = 64
            source = work / f'chain-{stages}.cc'
            lines = ['#include "cpphdl.h"', 'class Probe : public cpphdl::Module {',
                     'public:', 'using Word = cpphdl::logic<64>;', '_PORT(Word) data_in;',
                     '_PORT(Word) result_out = _ASSIGN(stage0_func());']
            for stage in range(stages):
                lines.append(f'Word stage{stage};')
                lines += [f'Word field{stage}_{index};' for index in range(fields)]
                incoming = 'data_in()' if stage + 1 == stages else f'stage{stage + 1}_func()'
                lines += [f'Word& stage{stage}_func() {{', f'stage{stage} = {incoming};']
                lines += [f'stage{stage} = stage{stage} ^ field{stage}_{index};'
                          for index in range(fields)]
                lines += [f'return stage{stage};', '}']
            lines += ['};', 'extern Probe cpphdl_top;']
            source.write_text('\n'.join(lines))
            started = time.monotonic()
            diagnostics = lower(source)
            counts = re.search(r'getter_reads=(\d+) getter_edges=(\d+)', diagnostics)
            assert counts, diagnostics[-5000:]
            assert int(counts[1]) == stages * (fields + 1) + 1, counts[0]
            assert int(counts[2]) == stages - 1, counts[0]
            print(f'{stages} stages: {counts[0]}, {time.monotonic() - started:.3f}s, '
                  f'peak child RSS {resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss} KiB')

        ast_bytes = []
        for count in (0, 256):
            lines = ['#include "cpphdl.h"',
                     'template<unsigned Index> class Leaf : public cpphdl::Module { public:',
                     'using Word = cpphdl::logic<64>;', '_PORT(Word) data_in;',
                     '_PORT(Word) result_out = _ASSIGN_REG(state);', 'cpphdl::reg<Word> state;',
                     'void _work(bool reset) { state._next = data_in(); }',
                     'void _strobe() { state.strobe(); }', '};',
                     'template<unsigned Dummy = 0> class Root : public cpphdl::Module { public:',
                     'using Word = cpphdl::logic<64>;', '_PORT(Word) data_in;']
            result = f'leaf{count - 1}.result_out()' if count else 'data_in()'
            lines += [f'_PORT(Word) result_out = _ASSIGN({result});']
            lines += [f'Leaf<{index}> leaf{index};' for index in range(count)]
            lines += ['void _assign() {']
            lines += [f'leaf{index}.data_in = _ASSIGN(data_in());' for index in range(count)]
            lines += ['}', 'void _work(bool reset) {']
            lines += [f'leaf{index}._work(reset);' for index in range(count)]
            lines += ['}', 'void _strobe() {']
            lines += [f'leaf{index}._strobe();' for index in range(count)]
            lines += ['}', '};', 'extern Root<> cpphdl_top;']
            source = work / f'ports-{count}.cc'
            source.write_text('\n'.join(lines))
            diagnostics = lower(source)
            allocation = re.search(r'ast_bytes=(\d+)', diagnostics)
            assert allocation, diagnostics[-5000:]
            ast_bytes.append(int(allocation[1]))
        growth = ast_bytes[1] - ast_bytes[0]
        # Subtract the library/header baseline; only reachable model bodies
        # should be instantiated, not each port's erased callable machinery.
        assert growth < 40 * 1024**2, ('excessive template AST growth', growth)
        print(f'256 templated port instances: {growth} additional AST bytes')

        peaks = []
        for stages in (256, 4096):
            source = work / f'construction-{stages}.cc'
            source.write_text(f'#define GRAPH_CONSTRUCTION_STAGES {stages}\n' +
                              Path(__file__).with_name('CppGraphConstruction.cc').read_text())
            started = time.monotonic()
            diagnostics = lower(source)
            counts = re.search(r'nodes=(\d+)', diagnostics)
            assert counts and int(counts[1]) < 20000, diagnostics[-5000:]
            peak = re.search(r'peak_locals=(\d+)', diagnostics)
            assert peak, diagnostics[-5000:]
            peaks.append(int(peak[1]))
            assert peaks[-1] < 32, ('dead loop locals retained', stages, peaks[-1])
            graph_text = source.with_suffix('.graph.cc').read_text()
            incoming = re.search(r'^"data" (.*) 1$', graph_text, re.MULTILINE)
            outgoing = re.search(r'^"result" (.*) 0$', graph_text, re.MULTILINE)
            assert incoming and outgoing and incoming[1] == outgoing[1], 'masked writes changed the data'
            print(f'{stages} repeated masked writes: {counts[0]}, {peak[0]}, '
                  f'{time.monotonic() - started:.3f}s', flush=True)
        assert peaks[0] == peaks[1], ('live locals scale with unrolled iterations', peaks)


if __name__ == '__main__':
    main()
