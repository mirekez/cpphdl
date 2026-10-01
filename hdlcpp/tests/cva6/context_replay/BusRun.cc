#include "BusTrace.h"
#include <chrono>
#include <cstring>
#include <cstdlib>
#ifdef USE_CPP_GRAPH
#define USE_NATIVE_GRAPH
#endif
#ifdef USE_VERILATOR
#include "VXbarBench.h"
#elif defined(USE_NATIVE_GRAPH)
#include "model.h"
#else
#include "XbarRoot.h"
#include "XbarRoot_optimized_combs.h"
long _system_clock = 0;
#endif

using Output = std::array<uint32_t, 128>;
struct Prepared {
    uint32_t reset_n;
#if defined(USE_VERILATOR) || defined(USE_NATIVE_GRAPH)
    std::array<uint32_t, 24> requests;
    std::array<uint32_t, 47> responses;
    explicit Prepared(const BusTrace& record)
        : reset_n(record.reset_n), requests(record.requests), responses(record.responses) {}
#else
    cpphdl::array<2, cpphdl::logic<374>, true> requests;
    cpphdl::array<10, cpphdl::logic<148>, true> responses;
    explicit Prepared(const BusTrace& record) : reset_n(record.reset_n) {
        std::memcpy(requests.data.bytes, record.requests.data(), requests.SIZE);
        std::memcpy(responses.data.bytes, record.responses.data(), responses.SIZE);
    }
#endif
};

class Bench {
#ifdef USE_VERILATOR
    VXbarBench model;
#elif defined(USE_NATIVE_GRAPH)
    cpphdl_native::Model model;
#else
    XbarRoot model;
#endif
public:
    Bench() {
#if !defined(USE_VERILATOR) && !defined(USE_NATIVE_GRAPH)
        model._assign();
#endif
    }
    Output step(const Prepared& input) {
        Output result{};
#ifdef USE_VERILATOR
        model.rst_ni = input.reset_n;
        std::memcpy(model.requests.data(), input.requests.data(), sizeof(input.requests));
        std::memcpy(model.responses.data(), input.responses.data(), sizeof(input.responses));
        model.clk_i = 0;
        model.eval();
        std::memcpy(result.data(), model.slave_outputs.data(), 40);
        std::memcpy(result.data() + 10, model.master_outputs.data(), 472);
        model.clk_i = 1;
        model.eval();
#elif defined(USE_NATIVE_GRAPH)
        model.rst_ni[0] = input.reset_n;
        model.requests = input.requests;
        model.responses = input.responses;
#ifdef USE_CPP_GRAPH
        model.work_reset[0] = !input.reset_n;
        // Ordinary CppHDL exposes work/strobe transactions, not SV clock pins.
        // Match its reset transaction before sampling, then commit and settle.
        if (!input.reset_n) model.step();
        else model.eval();
        std::memcpy(result.data(), model.slave_outputs.data(), 40);
        std::memcpy(result.data() + 10, model.master_outputs.data(), 472);
        model.step();
#else
        model.clk_i[0] = 0;
        model.step();
        std::memcpy(result.data(), model.slave_outputs.data(), 40);
        std::memcpy(result.data() + 10, model.master_outputs.data(), 472);
        model.clk_i[0] = 1;
        model.step();
#endif
#else
        model.reset_n = input.reset_n;
        model.requests = input.requests;
        model.responses = input.responses;
        if (!input.reset_n) {
            calc_all(model, true);
            commit_optimized_regs(model);
            ++_system_clock;
        }
        calc_all(model, !input.reset_n);
        std::memcpy(result.data(), model.slave_outputs.data.bytes, model.slave_outputs.SIZE);
        std::memcpy(result.data() + 10, model.master_outputs.data.bytes, model.master_outputs.SIZE);
        commit_optimized_regs(model);
        ++_system_clock;
#endif
        result[9] &= 15;
        result[127] &= 65535;
        return result;
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 3) return 2;
        char* end = nullptr;
        const auto repeats = std::strtoull(argv[2], &end, 10);
        if (!repeats || !end || *end) return 2;
        const auto records = read_bus_trace(argv[1]);
        std::vector<Prepared> inputs;
        inputs.reserve(records.size());
        Bench checker;
        for (std::size_t cycle = 0; cycle < records.size(); ++cycle) {
            inputs.emplace_back(records[cycle]);
            Output expected{};
            std::memcpy(expected.data(), records[cycle].slave_outputs.data(), 40);
            std::memcpy(expected.data() + 10, records[cycle].master_outputs.data(), 472);
            const auto actual = checker.step(inputs.back());
            if (actual != expected) {
                for (std::size_t word = 0; word < actual.size(); ++word) {
                    if (actual[word] != expected[word])
                        std::fprintf(stderr, "bus mismatch cycle=%zu word=%zu expected=%08x actual=%08x\n",
                                     cycle, word, expected[word], actual[word]);
                }
                return 1;
            }
        }
        Bench bench;
        uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (uint64_t repetition = 0; repetition < repeats; ++repetition)
            for (const auto& input : inputs)
                for (const auto word : bench.step(input)) checksum = (checksum * 0x100000001b3ull) ^ word;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("seconds=%.9f cycles=%llu checked=%zu checksum=%016llx\n", seconds,
                    (unsigned long long)(repeats * inputs.size()), inputs.size(), (unsigned long long)checksum);
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "%s\n", exception.what());
        return 2;
    }
}
