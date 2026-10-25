import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='word-slice-', dir=args.work) as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)

        def run(command, expected_error=None):
            result = subprocess.run(toolchain.command(command), env=toolchain.env,
                                    capture_output=True, text=True, timeout=180)
            if expected_error is not None:
                assert result.returncode and expected_error in result.stderr, (command, result.stdout, result.stderr)
                return
            if result.returncode:
                raise AssertionError((command, result.stdout, result.stderr))

        generator = work / 'generate.cpp'
        generator.write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    try {
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto data = graph.wire(129, "data", "input");
    auto offset = graph.wire(64, "offset", "input");
    graph.ports.push_back({"data", data, true});
    graph.ports.push_back({"offset", offset, true});
    Value selected32;
    for (unsigned width : {1u, 9u, 32u, 64u}) {
        auto selected = graph.add("word_slice", width, data, offset);
        graph.ports.push_back({"out" + std::to_string(width), selected, false});
        if (width == 32) selected32 = selected;
    }
    graph.ports.push_back({"constant", graph.add("word_slice", 64, data, constant(65, 64)), false});
    auto state = graph.wire(32, "registered", "state");
    graph.states.push_back({state, graph.binary("add", state, selected32, 32), {1}});
    graph.ports.push_back({"registered", state, false});
    auto initialized = graph.wire(32, "initialized", "state");
    graph.nodes[Graph::owner(initialized[0])].left = constant(17, 32);
    graph.ports.push_back({"initialized", initialized, false});
    const auto memo = std::stoul(argv[5]);
    if (memo == 5 || memo == 6) {
        graph.memories.push_back({"rom", 32, 4, {7, 19, 31, 43}});
        auto address = graph.binary("and", offset, constant(3, 64), 64);
        auto value = graph.add("memory_read", 32, address, constant(0, 64), constant(0, 64));
        graph.ports.push_back({"rom", value, false});
    }
    if (memo == 7) graph.memories.push_back({"ram", 32, 4});
    if (memo == 8) graph.add("host_random", 32, {}, {}, {1});
    if (memo)
        graph.emit(argv[1], std::stoul(argv[2]), std::stoul(argv[3]), false, false,
                   memo != 9, memo == 3 || memo == 4, memo == 2 || memo == 4 || memo == 6 || memo == 9,
                   std::stoul(argv[4]));
    else
        graph.emit(argv[1], std::stoul(argv[2]), std::stoul(argv[3]), false, false, std::stoul(argv[4]));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
''')
        run([args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include'), generator, '-o', work / 'generate'])
        runner = work / 'run.cpp'
        runner.write_text(r'''
#include "model.h"
#include <cassert>
int main() {
    cpphdl_native::Model model;
    uint32_t state = 0;
    uint64_t random = 0xc184947192389abfull;
    auto expected = [&](uint64_t offset, unsigned width) {
        if (offset > 129 - width) return uint64_t(0);
        uint64_t result = 0;
        for (unsigned bit = 0; bit < width; ++bit)
            result |= uint64_t((model.data[(offset + bit) / 32] >> ((offset + bit) % 32)) & 1) << bit;
        return result;
    };
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        if (sample % 5 != 0) {
            for (unsigned word = 0; word < 4; ++word) model.data[word] = uint32_t(random >> ((word % 2) * 32)) ^ word;
            model.data[4] = random & 1;
            uint64_t offset = sample % 31 ? sample % 161 : UINT64_MAX;
            model.offset = {uint32_t(offset), uint32_t(offset >> 32)};
        }
        uint64_t offset = uint64_t(model.offset[0]) | (uint64_t(model.offset[1]) << 32);
        for (bool commit : {false, true, false}) {
            model.eval(commit);
            assert(model.registered[0] == state);
            assert(model.initialized[0] == 17);
            assert(model.out1[0] == expected(offset, 1));
            assert(model.out9[0] == expected(offset, 9));
            assert(model.out32[0] == expected(offset, 32));
            assert((uint64_t(model.out64[0]) | (uint64_t(model.out64[1]) << 32)) == expected(offset, 64));
            assert((uint64_t(model.constant[0]) | (uint64_t(model.constant[1]) << 32)) == expected(65, 64));
#if TEST_ROM
            assert(model.rom[0] == 7 + (offset % 4) * 12);
#endif
            if (commit) state += uint32_t(expected(offset, 32));
        }
        if (sample % 97 == 0) {
            auto copy = model;
            copy.eval(false);
            assert(copy.registered[0] == state);
            assert(copy.initialized[0] == 17);
            assert(copy.out32 == model.out32);
        }
    }
}
''')
        for chunk, threads, split, memo in ((0, 1, 0, 0), (2, 1, 0, 0), (2, 1, 2, 0), (2, 2, 2, 0),
                                           (512, 1, 2, 1), (512, 2, 2, 1), (512, 2, 2, 2), (2, 2, 2, 2),
                                           (2, 1, 2, 3), (2, 2, 2, 4),
                                           (2, 1, 2, 5), (2, 2, 2, 6), (2, 1, 2, 7)):
            output = work / f'{chunk}-{threads}-{split}-{memo}'
            output.mkdir()
            run([work / 'generate', output / 'model.h', chunk, threads, split, memo])
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=address,undefined',
                 '-DTEST_ROM=' + str(int(memo in (5, 6))),
                 '-I' + str(repo / 'include'), '-I' + str(output), runner,
                 *sorted(output.glob('model.part.*.cpp')), '-o', output / 'run'])
            run([output / 'run'])
        for memo, chunk, error in ((8, 2, 'does not support host effects'),
                                    (9, 2, 'chunk memoization'),
                                    (1, 0, 'requires chunking')):
            header = work / f'rejected-{memo}-{chunk}.h'
            run([work / 'generate', header, chunk, 1, 0, memo], expected_error=error)
            assert not header.exists()
        print('native word slices: reads, invalid offsets, folding, split, lanes, memoization and mixed settle/work phases PASS')


if __name__ == '__main__':
    main()
