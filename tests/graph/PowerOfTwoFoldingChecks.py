"""Unsigned power-of-two folding, including late aliases and guarded divisors."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix='cpphdl-power-of-two-') as directory:
    work = Path(directory)
    toolchain = TestToolchain(args.cxx, None, work)

    def run(command):
        result = subprocess.run(toolchain.command(command), env=toolchain.env,
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(str(command) + '\n' + result.stdout + result.stderr)

    source = work / 'check.cpp'
    source.write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    for (const auto op : {"div", "mod", "mul"})
    for (unsigned width : {1u, 8u, 32u, 64u})
    for (unsigned inputWidth : {8u, 32u, 64u})
    for (unsigned shift : {0u, 1u, 7u, 31u, 63u})
    for (bool gated : {false, true})
    for (bool late : {false, true}) {
        Graph g;
        auto input = g.wire(inputWidth, "input", "input");
        auto enable = g.wire(1, "enable", "input");
        Value divisor(64, 0);
        divisor[shift] = gated ? enable[0] : 1;
        auto alias = late ? g.wire(64, "alias") : divisor;
        auto result = g.binary(op, input, alias, width);
        if (late) g.connect(alias, divisor);
        g.ports.push_back({"out", result, false});
        g.optimize();
        for (auto index : g.dependencyOrder())
            assert(g.nodes[index].op != "div" && g.nodes[index].op != "mod" && g.nodes[index].op != "mul");
        for (bool on : {false, true}) {
            auto evaluated = g;
            Value data(inputWidth);
            for (unsigned bit = 0; bit < data.size(); ++bit) data[bit] = (bit % 3) != 1;
            evaluated.connect(input, data);
            evaluated.connect(enable, constant(on, 1));
            evaluated.optimize();
            Value expected(width, 0);
            for (unsigned bit = 0; bit < width; ++bit) {
                if (gated && !on) continue;
                if (std::string(op) == "div" && bit + shift < data.size()) expected[bit] = data[bit + shift];
                if (std::string(op) == "mod" && bit < shift && bit < data.size()) expected[bit] = data[bit];
                if (std::string(op) == "mul" && bit >= shift && bit - shift < data.size()) expected[bit] = data[bit - shift];
            }
            assert(evaluated.resolved(result) == expected);
        }
    }
    Graph g;
    auto input = g.wire(32, "input", "input"), alias = g.wire(64, "alias");
    g.ports.push_back({"input", input, true});
    // Constant on the left of multiplication, revealed only after construction.
    g.ports.push_back({"out", g.binary("mul", alias, input, 64), false});
    g.connect(alias, constant(32, 64));
    g.writeCpp(argv[1]);
}
''')
    flags = [args.cxx, '-std=c++23', '-O1', '-I' + str(root / 'include')]
    run([*flags, source, '-o', work / 'check'])
    run([work / 'check', work / 'serialized.cpp'])
    run([*flags, work / 'serialized.cpp', '-o', work / 'emit'])
    run([work / 'emit', work / 'model.h'])
    runner = work / 'run.cpp'
    runner.write_text('''#include "model.h"
#include <cassert>
int main() {
    cpphdl_native::Model m;
    for (unsigned i = 0; i < 4096; ++i) {
        m.input[0] = i * 7919u; m.eval();
        assert((uint64_t(m.out[0]) | (uint64_t(m.out[1]) << 32)) == uint64_t(m.input[0]) * 32);
    }
}
''')
    run([*flags, runner, '-o', work / 'run'])
    run([work / 'run'])
print('power-of-two folding: eager/late, guarded, mixed widths, serialization and native oracle PASS')
