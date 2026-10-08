#include "EmptyRepeatCast.cc"
#include <cstdio>
#ifdef EMPTY_GRAPH
#include "model.h"
#endif
#ifdef EMPTY_RTL
#include "VEmptyRepeatCast.h"
#endif

long _system_clock = 0;
struct Driver {
    EmptyRepeatCast<EMPTY_REPEAT_COUNT> dut;
    logic<32> pc;
    void _assign() {
        dut.pc_in = _ASSIGN(pc);
        dut._assign();
    }
};

int main() {
    Driver driver;
    uint32_t random = 0x89257631;
    constexpr uint64_t mask = (uint64_t(1) << EMPTY_REPEAT_COUNT) - 1;
#ifdef EMPTY_GRAPH
    cpphdl_native::Model graph;
    auto read64 = [](const auto& words) {
        return uint64_t(words[0]) | (uint64_t(words[1]) << 32);
    };
#endif
#ifdef EMPTY_RTL
    VEmptyRepeatCast rtl;
#endif
    driver._assign();
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t pc = sample < 256 ? sample * 0x01010101u : random;
        const uint64_t scalar = (pc >> 31) ? mask : 0;
        const uint32_t result = pc | uint32_t(scalar);
        const uint64_t concat = (scalar << 32) | pc;
        const unsigned seed = pc & 15;
        const unsigned taken = (pc >> 4) & 1;
        const uint64_t first = ((seed + 1) & 1) ? mask : 0;
        const uint64_t second = taken && ((seed + 3) & 1) ? mask : 0;
        const uint64_t effects = (uint64_t(seed + 2 + taken) << 48) | (first << 16) | second;
        driver.pc = pc;
        ++_system_clock;
        bool good = uint64_t(driver.dut.result_out()) == result &&
            uint64_t(driver.dut.scalar_out()) == scalar &&
            uint64_t(driver.dut.concat_out()) == concat &&
            uint64_t(driver.dut.narrow_out()) == uint32_t(scalar) &&
            uint64_t(driver.dut.truth_out()) == bool(scalar) &&
            uint64_t(driver.dut.effects_out()) == effects;
#ifdef EMPTY_GRAPH
        graph.pc[0] = pc;
        graph.eval();
        good &= graph.result[0] == result && read64(graph.scalar) == scalar &&
            read64(graph.concat) == concat &&
            graph.narrow[0] == uint32_t(scalar) && graph.truth[0] == bool(scalar) &&
            read64(graph.effects) == effects;
#endif
#ifdef EMPTY_RTL
        rtl.pc_in = pc;
        rtl.eval();
        good &= rtl.result_out == result && rtl.scalar_out == scalar &&
            rtl.concat_out == concat &&
            rtl.narrow_out == uint32_t(scalar) && rtl.truth_out == bool(scalar) &&
            rtl.effects_out == effects;
#endif
        if (!good) {
            std::fprintf(stderr, "empty repeat cast mismatch: count=%u pc=%08x\n", EMPTY_REPEAT_COUNT, pc);
            return 1;
        }
    }
    std::printf("repeat<%u>: 4096 samples matched, including operand side effects\n", EMPTY_REPEAT_COUNT);
}
