"""Settling retains output/read-check cones, not transactional producers."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='native-phase-', dir=args.work) as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)

        def run(command):
            result = subprocess.run(toolchain.command(command), env=toolchain.env,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise AssertionError((command, result.stdout, result.stderr))
            return result.stdout

        generator = work / 'generator.cpp'
        generator.write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto input = [&](const char* name, unsigned width) {
        auto bits = graph.wire(width, name, "input");
        graph.ports.push_back({name, bits, true});
        return bits;
    };
    auto data = input("data", 32), address = input("address", 8);
    auto check = input("check", 1), fail = input("fail", 1);
    graph.memories.push_back({"ram", 32, 2});
    unsigned mode = std::stoul(argv[5]);
    bool combinational = mode == 1;
    auto adjustedAddress = graph.binary("add", address, constant(1, 8), 8);
    graph.memoryAccesses.push_back({0, adjustedAddress, check, mode == 0});
    graph.memoryWrites.push_back({0, adjustedAddress, data, check});
    graph.add("assert_failure", 1, fail, {}, {}, "work assertion");
    for (unsigned branch = 0; branch < 2; ++branch) {
        auto state = graph.wire(32, "register" + std::to_string(branch), "state");
        auto value = graph.binary("xor", state, data, 32);
        for (unsigned stage = 0; stage < 64; ++stage) {
            value = graph.binary("add", value, constant(stage + branch + 1, 32), 32);
            value = graph.binary("mul", value, constant(3, 32), 32);
        }
        graph.states.push_back({state, value, {1}});
        if (combinational) {
            auto read = graph.add("memory_read", 32, adjustedAddress, constant(0, 64), constant(0, 64));
            state = graph.binary("xor", state, read, 32);
            state = graph.binary("add", state, data, 32);
        }
        graph.ports.push_back({"out" + std::to_string(branch), state, false});
    }
    graph.emit(argv[1], std::stoul(argv[2]), std::stoul(argv[3]), false, false, std::stoul(argv[4]));
}
''')
        run([args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include'), generator,
             '-o', work / 'generator'])
        runner = work / 'runner.cpp'
        runner.write_text(r'''
#include "model.h"
#include <cassert>
int main() {
    cpphdl_native::Model model;
    std::array<uint32_t, 2> expected{};
    uint32_t memory = 0;
    for (unsigned sample = 1; sample <= 128; ++sample) {
        model.data[0] = sample * 17;
        model.address[0] = 0;
        model.check[0] = sample % 2;
        model.fail[0] = 0;
        for (unsigned branch = 0; branch < 2; ++branch) {
            expected[branch] ^= model.data[0];
            for (unsigned stage = 0; stage < 64; ++stage)
                expected[branch] = (expected[branch] + stage + branch + 1) * 3u;
        }
        if (model.check[0]) memory = model.data[0];
        model.step();
        assert(model.out0[0] == (COMBINATIONAL ? (expected[0] ^ memory) + model.data[0] : expected[0]));
        assert(model.out1[0] == (COMBINATIONAL ? (expected[1] ^ memory) + model.data[0] : expected[1]));
#if CHUNKED
        auto previous = model.__cpphdl_values;
#endif
        model.data[0] += 100;
        model.fail[0] = 1;
        model.eval(false);
        assert(model.out0[0] == (COMBINATIONAL ? (expected[0] ^ memory) + model.data[0] : expected[0]));
#if CHUNKED
        for (auto slot : DEAD_SLOTS) assert(model.__cpphdl_values[slot] == previous[slot]);
        if (!READ_CHECKS) assert(__cpphdl_settle_calls == 0);
#endif
        auto before = model.memory0;
        bool threw = false;
        try { model.eval(true); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && model.memory0 == before);
        model.fail[0] = 0;
        model.address[0] = 254;
        model.check[0] = 1;
        threw = false;
        try { model.eval(false); } catch (const std::out_of_range&) { threw = true; }
        assert(threw == bool(READ_CHECKS));
        threw = false;
        try { model.eval(true); } catch (const std::out_of_range&) { threw = true; }
        assert(threw && model.memory0 == before);
        model.check[0] = 0;
        model.eval(false);
    }
}
''')
        for mode in (0, 1, 2):
            for chunk, threads, split in ((0, 1, 0), (1, 1, 0), (7, 1, 0),
                                          (16, 1, 4), (16, 2, 4)):
                output = work / f'model-{mode}-{chunk}-{threads}-{split}'
                output.mkdir()
                header = output / 'model.h'
                run([work / 'generator', header, chunk, threads, split, mode])
                parts = sorted(output.glob('model.part.*.cpp'))
                sources = [header, *parts]
                dead_slots = set()
                for source in sources:
                    text = source.read_text()
                    dead_slots.update(map(int, re.findall(
                        r'if constexpr\(Commit\) \{\n__cpphdl_values\[(\d+)\] =', text)))
                    text = re.sub(r'(__cpphdl_chunk_\d+\(\) \{\n)',
                                  r'\1if constexpr(!Commit) ++__cpphdl_settle_calls;\n', text)
                    if source == header:
                        text = text.replace('#pragma once\n',
                                            '#pragma once\n#include <atomic>\ninline std::atomic<unsigned> __cpphdl_settle_calls{0};\n')
                    source.write_text(text)
                if chunk:
                    assert dead_slots, 'fixture must contain cross-chunk work-only stores'
                slots = ','.join(map(str, sorted(dead_slots)))
                run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=address,undefined',
                     '-I' + str(repo / 'include'), '-I' + str(output),
                     '-DCOMBINATIONAL=' + str(int(mode == 1)), '-DREAD_CHECKS=' + str(int(mode != 0)),
                     '-DCHUNKED=' + str(int(bool(chunk))),
                     '-DDEAD_SLOTS=(std::array<unsigned,' + str(len(dead_slots)) + '>{' + slots + '})',
                     runner, *parts, '-o', output / 'run'])
                run([output / 'run'])
                print(f'phase roots: mode={mode} chunk={chunk} threads={threads} split={split} PASS', flush=True)


if __name__ == '__main__':
    main()
