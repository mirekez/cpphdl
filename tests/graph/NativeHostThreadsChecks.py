"""Host effects stay unique and caller-owned while independent cones run in workers."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='native-host-threads-', dir=args.work) as directory:
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
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto data = graph.wire(32, "data", "input"), enabled = graph.wire(1, "enabled", "input");
    graph.ports.push_back({"data", data, true}); graph.ports.push_back({"enabled", enabled, true});
    auto effect = graph.add("host_random", 32, {}, {}, {1});
    Value arguments;
    for (unsigned width : {8u, 8u, 32u, 32u, 32u, 8u, 8u, 32u, 32u}) {
        auto field = width == 32 ? data : constant(0, width);
        arguments.insert(arguments.end(), field.begin(), field.end());
    }
    for (unsigned callback = 0; callback < 2; ++callback) {
        effect = graph.add("host_debug_tick", 32, {effect.front()}, arguments, enabled);
        auto output = graph.add("host_debug_output", 32, {effect.front()}, {}, constant(4, 4));
        auto state = graph.wire(32, "host_state" + std::to_string(callback), "state");
        graph.states.push_back({state, output, enabled});
        graph.ports.push_back({"host" + std::to_string(callback), state, false});
    }
    graph.add("host_random", 32, {effect.front()}, {}, {1});
    for (unsigned branch = 0; branch < 6; ++branch) {
        auto state = graph.wire(32, "pure_state" + std::to_string(branch), "state");
        auto next = graph.binary("xor", state, data, 32);
        for (unsigned stage = 0; stage < 32; ++stage) {
            next = graph.binary("add", next, constant(stage + branch + 1, 32), 32);
            next = graph.binary("mul", next, constant(3, 32), 32);
        }
        graph.states.push_back({state, next, {1}});
        graph.ports.push_back({"pure" + std::to_string(branch), state, false});
    }
    graph.emit(argv[1], 9, std::stoul(argv[2]), false, false, std::stoul(argv[3]));
}
''')
        run([args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include'), generator, '-o', work / 'generate'])
        runner = work / 'run.cpp'
        runner.write_text(r'''
#include "model.h"
#include <cassert>
#include <thread>
static const auto caller = std::this_thread::get_id();
static unsigned randomCalls = 0, debugCalls = 0, event = 0;
extern "C" long random() noexcept {
    assert(std::this_thread::get_id() == caller);
    assert(event == 0 || event == 1 || event == 3);
    ++randomCalls; ++event;
    return randomCalls;
}
extern "C" int debug_tick(unsigned char* valid, unsigned char, int*, int*, int* data,
                           unsigned char, unsigned char*, int, int) {
    assert(std::this_thread::get_id() == caller);
    assert(event == 1 || event == 2);
    ++event; ++debugCalls;
    *valid = 1; *data += 13 * debugCalls;
    return debugCalls;
}
int main() {
    cpphdl_native::Model model;
#if EXPECT_THREADS > 1
    static_assert(cpphdl_native::Model::__cpphdl_thread_count == EXPECT_THREADS);
#endif
    std::array<uint32_t, 6> expected{};
    uint32_t first = 0, second = 0;
    unsigned expectedDebug = 0;
    for (unsigned sample = 1; sample <= 1024; ++sample) {
        model.data[0] = sample * 17; model.enabled[0] = sample % 3 != 0;
        event = 0;
        model.eval(false); model.eval(false);
        assert(event == 0);
        for (unsigned branch = 0; branch < 6; ++branch) {
            expected[branch] ^= model.data[0];
            for (unsigned stage = 0; stage < 32; ++stage)
                expected[branch] = (expected[branch] + stage + branch + 1) * 3u;
        }
        if (model.enabled[0]) {
            first = model.data[0] + 13 * ++expectedDebug;
            second = model.data[0] + 13 * ++expectedDebug;
        }
        model.step();
        assert(randomCalls == sample * 2 && debugCalls == expectedDebug);
        assert(event == (model.enabled[0] ? 4u : 2u));
        assert(model.host0[0] == first && model.host1[0] == second);
        assert(model.pure0[0] == expected[0] && model.pure1[0] == expected[1]);
        assert(model.pure2[0] == expected[2] && model.pure3[0] == expected[3]);
        assert(model.pure4[0] == expected[4] && model.pure5[0] == expected[5]);
    }
}
''')
        for threads, split in ((1, 0), (2, 0), (4, 0), (4, 3)):
            output = work / f'{threads}-{split}'
            output.mkdir()
            run([work / 'generate', output / 'model.h', threads, split])
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=undefined',
                 '-fno-sanitize-recover=all', '-DEXPECT_THREADS=' + str(threads),
                 '-I' + str(repo / 'include'), '-I' + str(output), runner,
                 *sorted(output.glob('model.part.*.cpp')), '-o', output / 'run'])
            run([output / 'run'])
        print('native host threads: ordered unique caller callbacks, disabled calls, unused effects, settling and split workers PASS')


if __name__ == '__main__':
    main()
