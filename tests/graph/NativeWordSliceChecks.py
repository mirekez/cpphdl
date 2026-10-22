import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='word-slice-', dir=args.work) as directory:
        work = Path(directory)

        def run(command):
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise AssertionError((command, result.stdout, result.stderr))

        generator = work / 'generate.cpp'
        generator.write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    Graph graph;
    auto data = graph.wire(129, "data", "input");
    auto offset = graph.wire(64, "offset", "input");
    graph.ports.push_back({"data", data, true});
    graph.ports.push_back({"offset", offset, true});
    for (unsigned width : {1u, 9u, 32u, 64u}) {
        auto selected = graph.add("word_slice", width, data, offset);
        graph.ports.push_back({"out" + std::to_string(width), selected, false});
    }
    graph.ports.push_back({"constant", graph.add("word_slice", 64, data, constant(65, 64)), false});
    graph.emit(argv[1], std::stoul(argv[2]), std::stoul(argv[3]), false, false, std::stoul(argv[4]));
}
''')
        run([args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include'), generator, '-o', work / 'generate'])
        runner = work / 'run.cpp'
        runner.write_text(r'''
#include "model.h"
#include <cassert>
int main() {
    cpphdl_native::Model model;
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
        for (unsigned word = 0; word < 4; ++word) model.data[word] = uint32_t(random >> ((word % 2) * 32)) ^ word;
        model.data[4] = random & 1;
        uint64_t offset = sample % 31 ? sample % 161 : UINT64_MAX;
        model.offset = {uint32_t(offset), uint32_t(offset >> 32)};
        model.eval();
        assert(model.out1[0] == expected(offset, 1));
        assert(model.out9[0] == expected(offset, 9));
        assert(model.out32[0] == expected(offset, 32));
        assert((uint64_t(model.out64[0]) | (uint64_t(model.out64[1]) << 32)) == expected(offset, 64));
        assert((uint64_t(model.constant[0]) | (uint64_t(model.constant[1]) << 32)) == expected(65, 64));
    }
}
''')
        for chunk, threads, split in ((0, 1, 0), (2, 1, 0), (2, 1, 2), (2, 2, 2)):
            output = work / f'{chunk}-{threads}-{split}'
            output.mkdir()
            run([work / 'generate', output / 'model.h', chunk, threads, split])
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=address,undefined',
                 '-I' + str(repo / 'include'), '-I' + str(output), runner,
                 *sorted(output.glob('model.part.*.cpp')), '-o', output / 'run'])
            run([output / 'run'])
        print('native word slices: cross-word/odd-width/full-width reads, invalid offsets, folding, split and lanes PASS')


if __name__ == '__main__':
    main()
