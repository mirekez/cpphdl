"""Exercise configurable executor waits, parked workers and slow/throwing hosts."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-wait-policy-') as temp:
    work = Path(temp)
    toolchain = TestToolchain(args.cxx, None, work)
    source = work / 'check.cpp'
    source.write_text(r'''
#include <cpphdl_graph_threads.h>
#include <array>
#include <cassert>
#include <chrono>
#include <thread>
using namespace cpphdl::graph_runtime;
struct Work {
    uint64_t input = 0;
    bool slow = false;
    std::array<uint64_t, 4> output{};
};
int main(int argc, char** argv) {
    if (std::string_view(argv[1]) == "invalid") {
        try { Threads executor(4, 4); }
        catch (const std::invalid_argument&) { return 0; }
        return 1;
    }
    assert(nativeSpinLimit() == std::stoul(argv[1]));
    Threads executor(4, 4);
    // A constructed executor must never read the setting again.
    setenv("CPPHDL_NATIVE_SPIN", "invalid-after-construction", 1);
    setenv("CPPHDL_NATIVE_SPIN_LIMIT", "invalid-after-construction", 1);
    Work work;
    auto lane = [](void* opaque, Threads& pool, unsigned index, uint32_t epoch) noexcept {
        auto& w = *static_cast<Work*>(opaque);
        if (index) pool.wait(index - 1, epoch);
        if (w.slow && index) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        w.output[index] = index ? w.output[index - 1] + 1 : w.input;
        pool.complete(index, epoch);
    };
    for (unsigned i = 0; i < 96; ++i) {
        const unsigned active = 1 + i % 4;
        work.input = i * 101;
        work.slow = i % 19 == 0;
        const auto before = work.output;
        bool threw = false;
        try {
            executor.run(&work, lane, active, [&] {
                assert(work.output[0] == work.input);
                if (i % 17 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if (i % 11 == 0) throw std::runtime_error("host");
            });
        } catch (const std::runtime_error&) { threw = true; }
        assert(threw == (i % 11 == 0));
        for (unsigned j = 0; j < active; ++j) assert(work.output[j] == work.input + j);
        for (unsigned j = active; j < 4; ++j) assert(work.output[j] == before[j]);
        if (i % 13 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    // Destruction must wake and join workers that have become idle.
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
}
''')
    command = [args.cxx, '-std=c++23', '-O2', '-pthread', '-fsanitize=undefined',
               '-fno-sanitize-recover=all', '-I' + str(root / 'include'),
               str(source), '-o', str(work / 'check')]
    subprocess.run(toolchain.command(command), env=toolchain.env, check=True, timeout=120)
    base_env = dict(toolchain.env)
    for name in ('CPPHDL_NATIVE_SPIN', 'CPPHDL_NATIVE_SPIN_LIMIT'):
        base_env.pop(name, None)
    for name, maximum in (('CPPHDL_NATIVE_SPIN', 65536), ('CPPHDL_NATIVE_SPIN_LIMIT', 1000000)):
        for value, expected in [(None, '4096'), ('0', '0'), ('1', '1'), ('64', '64'),
                                ('4096', '4096'), (str(maximum), str(maximum))]:
            env = dict(base_env)
            if value is not None:
                env[name] = value
            subprocess.run(toolchain.command([str(work / 'check'), expected]),
                           env=env, check=True, timeout=30)
        for value in ('', '-1', '+64', '64x', ' 64', str(maximum + 1), '4294967296', '9999999999999999999999'):
            env = dict(base_env, **{name: value})
            subprocess.run(toolchain.command([str(work / 'check'), 'invalid']),
                           env=env, check=True, timeout=30)
    for primary, expected in (('0', '0'), ('64', '64'), ('invalid', 'invalid')):
        env = dict(base_env, CPPHDL_NATIVE_SPIN=primary, CPPHDL_NATIVE_SPIN_LIMIT='4096')
        subprocess.run(toolchain.command([str(work / 'check'), expected]),
                       env=env, check=True, timeout=30)
    print('native wait policy: defaults, bounds, snapshot, parked/skipped workers, task dependencies, slow/throwing hosts and shutdown PASS')
