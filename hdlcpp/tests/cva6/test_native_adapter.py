"""Check native testbench preparation, commit accounting and SRAM preload APIs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('CPPHDL_TEST_CXX'), 'set CPPHDL_TEST_CXX for the native adapter check')
class NativeAdapterTest(unittest.TestCase):
    def test_driver_boundary(self):
        support = Path(__file__).resolve().parent
        repo = support.parents[2]
        with tempfile.TemporaryDirectory(prefix='cva6-native-adapter-', dir='/tmp') as directory:
            work = Path(directory)
            source = work / 'graph.cpp'
            source.write_text(r'''
#include <cpphdl_graph_native.h>
int main(int, char** argv) {
    using namespace cpphdl::graph;
    Graph graph;
    auto input = [&](const char* name, unsigned width) {
        auto bits = graph.wire(width, name, "input");
        graph.ports.push_back({name, bits, true}); return bits;
    };
    auto reset = input("work_reset", 1);
    input("rst_ni", 1);
    auto rtc = input("rtc", 1);
    auto limit = input("cycle_limit", 32);
    input("tohost", 32);
    auto counter = graph.wire(32, "counter", "state");
    graph.states.push_back({counter, graph.mux(reset, constant(0, 32),
        graph.binary("add", counter, constant(1, 32), 32)), {1}});
    graph.ports.push_back({"exit", counter, false});
    auto commit = Value(256, 0);
    for (unsigned bit = 0; bit < 32; ++bit) commit[bit] = counter[bit];
    commit[255] = 1;
    graph.ports.push_back({"commit0", commit, false});
    graph.ports.push_back({"commit1", Value(256, 0), false});
    graph.memories.push_back({"sram", 64, 17});
    graph.memoryWrites.push_back({0, constant(1, 64),
        graph.binary("add", limit, rtc, 64), graph.unary("not", reset)});
    graph.emit(argv[1], 1);
}
''')
            runner = work / 'run.cpp'
            runner.write_text(r'''
#include "run_cpphdl_testharness_model.h"
#include <cassert>
int main() {
    cpphdl_model_create(); cpphdl_model_assign();
    cpphdl_model_set_reset(false); cpphdl_model_set_rtc(false);
    for (unsigned cycle = 0; cycle < 10; ++cycle) {
        cpphdl_model_work(true); assert(cpphdl_model_exit() == 0);
        cpphdl_model_strobe(); assert(cpphdl_model_exit() == 0);
    }
    cpphdl_model_set_reset(true); cpphdl_model_configure(37, 0x80001000);
    cpphdl_model_write_byte(2, 0xab); cpphdl_model_apply_memory();
    assert(cpphdl_model_read_word(0) == 0xab0000);
    cpphdl_model_work(false); assert(cpphdl_model_exit() == 0);
    cpphdl_model_set_rtc(true);
    cpphdl_model_strobe(); assert(cpphdl_model_exit() == 1);
    assert(cpphdl_model_read_word(8) == 37);
    cpphdl_model_work(false); assert(cpphdl_model_exit() == 1);
    cpphdl_model_strobe(); assert(cpphdl_model_exit() == 2);
    assert(cpphdl_model_read_word(8) == 38);
    assert(cpphdl_model_commit(0).valid);
    assert(cpphdl_model_commit(0).order == 2);
    assert(!cpphdl_model_commit(1).valid);
}
''')
            (work / 'native_memory.h').write_text('#define CVA6_GRAPH_SRAM memory0\n')
            compiler = os.environ['CPPHDL_TEST_CXX']
            flags = [compiler, '-std=c++23', '-O2', '-I' + str(repo / 'include')]
            subprocess.run([*flags, source, '-o', work / 'emit'], check=True, timeout=180)
            subprocess.run([work / 'emit', work / 'model.h'], check=True, timeout=30)
            subprocess.run([*flags, '-I' + str(work), '-I' + str(support / 'support/cpphdl'),
                            runner, support / 'run_native_graph_model.cpp', '-o', work / 'run'],
                           check=True, timeout=180)
            environment = os.environ.copy()
            environment.pop('CVA6_BENCH_STATS', None)
            subprocess.run([work / 'run'], env=environment, check=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
