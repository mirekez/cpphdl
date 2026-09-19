#ifdef USE_VERILATOR
#include "VRegfileBench.h"
#else
#include "RegfileRoot.h"
#include "RegfileRoot_optimized_combs.h"
long _system_clock = 0;
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "RegfileTrace.h"

class Bench {
#ifdef USE_VERILATOR
    VRegfileBench model;
#else
    RegfileRoot model;
#endif
    uint32_t random = 0x6d2b79f5;
public:
    Bench() {
#ifndef USE_VERILATOR
        model._assign();
#endif
    }
    uint64_t step(uint64_t cycle) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        RegfileTrace input{};
        input.read_addresses = random & 1023;
        input.write_addresses = (random >> 10) & 1023;
        input.write_data = uint64_t(random) << 32 | uint32_t(cycle);
        input.enables = random >> 30;
        input.reset_n = cycle >= 4;
        return step(input);
    }
    uint64_t step(const RegfileTrace& input) {
#ifdef USE_VERILATOR
        model.rst_ni = input.reset_n;
        model.read_addresses = input.read_addresses;
        model.write_addresses = input.write_addresses;
        model.write_data = input.write_data;
        model.enables = input.enables;
        model.clk_i = 0;
        model.eval();
        const uint64_t result = model.read_data;
        model.clk_i = 1;
        model.eval();
#else
        model.reset_n = input.reset_n;
        model.read_addresses = input.read_addresses;
        model.write_addresses = input.write_addresses;
        model.write_data = input.write_data;
        model.enables = input.enables;
        // Settle asynchronous reset before observing the pre-edge read ports.
        if (!input.reset_n) {
            calc_all(model, true);
            commit_optimized_regs(model);
            ++_system_clock;
        }
        calc_all(model, !input.reset_n);
        const uint64_t result = uint64_t(model.read_data.data);
        commit_optimized_regs(model);
        ++_system_clock;
#endif
        return result;
    }
};

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    std::vector<RegfileTrace> trace;
    if (argc == 4) {
        auto* input = std::fopen(argv[3], "rb");
        if (!input) return 2;
        RegfileTrace record;
        size_t bytes;
        while ((bytes = std::fread(&record, 1, sizeof(record), input)) == sizeof(record)) {
            if (record.read_addresses > 1023 || record.write_addresses > 1023 ||
                record.enables > 3 || record.reset_n > 1 || record.reserved) {
                std::fclose(input);
                return 2;
            }
            trace.push_back(record);
        }
        const bool valid = !bytes && !std::ferror(input) && !trace.empty();
        std::fclose(input);
        if (!valid || trace.front().reset_n) return 2;
    }
#ifdef USE_VERILATOR
    auto* file = std::fopen(argv[1], "wb");
#else
    auto* file = std::fopen(argv[1], "rb");
#endif
    if (!file) return 2;
    Bench checker;
    const auto checked = trace.empty() ? 20000 : trace.size();
    for (size_t cycle = 0; cycle < checked; ++cycle) {
        const auto actual = trace.empty() ? checker.step(cycle) : checker.step(trace[cycle]);
        if (!trace.empty() && actual != trace[cycle].expected) {
            std::fprintf(stderr, "full-CVA6 mismatch cycle=%zu expected=%llx actual=%llx\n", cycle,
                         (unsigned long long)trace[cycle].expected, (unsigned long long)actual);
            return 1;
        }
#ifdef USE_VERILATOR
        if (std::fwrite(&actual, sizeof(actual), 1, file) != 1) return 2;
#else
        uint64_t expected;
        if (std::fread(&expected, sizeof(expected), 1, file) != 1) return 2;
        if (actual != expected) {
            std::fprintf(stderr, "mismatch cycle=%zu RTL=%llx cpphdl=%llx\n", cycle,
                         (unsigned long long)expected, (unsigned long long)actual);
            return 1;
        }
#endif
    }
#ifndef USE_VERILATOR
    if (std::fgetc(file) != EOF) return 2;
#endif
    if (std::fclose(file)) return 2;
    Bench bench;
    uint64_t checksum = 0;
    const auto cycles = std::strtoull(argv[2], nullptr, 10);
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t cycle = 0; cycle < cycles; ++cycle)
        checksum = checksum * 0x100000001b3ull ^
                   (trace.empty() ? bench.step(cycle) : bench.step(trace[cycle % trace.size()]));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("seconds=%.9f checksum=%016llx cycles=%llu checked=%zu\n", seconds,
                (unsigned long long)checksum, (unsigned long long)cycles, checked);
}
