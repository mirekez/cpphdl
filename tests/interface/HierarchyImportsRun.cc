#include "HierarchyImports.cc"
#include <cstdio>
#ifdef IMPORTS_VERILATOR
#include "VHierarchyImports.h"
#endif

long _system_clock = 0;
struct ImportDriver : Module {
    logic<32> seed;
    HierarchyImports dut;
    void _assign() {
        dut.seed_in = _ASSIGN(seed);
        dut._assign();
    }
};

static uint32_t response(uint32_t seed) {
    return uint16_t(seed + 2) | (uint32_t(uint8_t((seed >> 16) ^ 0x5a)) << 16);
}

int main() {
    ImportDriver driver;
#ifdef IMPORTS_VERILATOR
    VHierarchyImports rtl;
#endif
    uint32_t random = 0x12345678;
    driver._assign();
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        uint32_t seed = sample == 0 ? 0 : sample == 1 ? ~0u : random;
        uint32_t expected = response(seed);
        uint32_t sum = response(seed + 1) + response(seed + 0x10003);
        driver.seed = seed;
        ++_system_clock;
        if (uint32_t(driver.dut.pass_out()) != seed ||
            uint32_t(driver.dut.response_out()) != expected ||
            uint32_t(driver.dut.lane_sum_out()) != sum ||
            uint32_t(driver.dut.assigned_out()) != (seed & 0xffffff)) {
            std::fprintf(stderr, "C++ hierarchy mismatch at sample %u\n", sample);
            return 1;
        }
#ifdef IMPORTS_VERILATOR
        rtl.clk = 0;
        rtl.reset = 0;
        rtl.seed_in = seed;
        rtl.eval();
        if (rtl.pass_out != seed || rtl.response_out != expected || rtl.lane_sum_out != sum ||
            rtl.assigned_out != (seed & 0xffffff)) {
            std::fprintf(stderr, "RTL hierarchy mismatch at sample %u: got %x/%x/%x/%x\n",
                         sample, rtl.pass_out, rtl.response_out, rtl.lane_sum_out, rtl.assigned_out);
            return 2;
        }
#endif
    }
    std::puts("hierarchical struct/interface ports: 4096 samples passed");
}
