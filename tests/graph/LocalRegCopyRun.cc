#include "LocalRegCopy.cc"
#include <cstdio>
#ifdef REG_GRAPH
#include "model.h"
#endif
#ifdef REG_RTL
#include "VLocalRegCopy.h"
#endif
long _system_clock = 0;
struct Driver {
    LocalRegCopy dut;
    logic<8> data;
    void _assign() {
        dut.data_in = _ASSIGN(data);
        dut._assign();
    }
};

int main() {
    Driver driver;
    uint32_t random = 0x89276153;
    unsigned previous = 0;
#ifdef REG_GRAPH
    cpphdl_native::Model graph;
#endif
#ifdef REG_RTL
    VLocalRegCopy rtl;
#endif
    driver._assign();
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const bool reset = sample % 97 == 0;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const unsigned input = sample < 256 ? sample : random & 255;
        const unsigned current = reset ? 0 : input ^ 0x3c;
        const unsigned last = COPY_LANES == 1 ? current : reset ? 0 : (input + COPY_LANES-1) & 255;
        const unsigned a = input ^ 0xa5, b = input ^ 0x5a;
        const uint64_t expected_audit = reset ? 0 : uint64_t(a) | (uint64_t(b) << 8) |
            (uint64_t((previous+1)&255) << 16) | (uint64_t((input+2)&255) << 24) |
            (uint64_t(previous) << 32) | (uint64_t(b) << 40) |
            (uint64_t((a+3)&255) << 48) | (uint64_t((b + ((input&1) ? 4 : 7))&255) << 56);
        driver.data = input;
        ++_system_clock;
#ifdef REG_RTL
        rtl.clk = 0;
        rtl.reset = reset;
        rtl.data_in = input;
        rtl.eval();
#endif
        driver.dut._work(reset);
#ifdef REG_RTL
        rtl.clk = 1;
        rtl.eval();
#endif
        driver.dut._strobe();
        ++_system_clock;
        bool good = uint64_t(driver.dut.result_out()) == current &&
            uint64_t(driver.dut.last_out()) == last && uint64_t(driver.dut.audit_out()) == expected_audit &&
            uint64_t(driver.dut.committed_copy_out()) == (current ^ 0xe7);
#ifdef REG_GRAPH
        graph.data[0] = input;
        graph.work_reset[0] = reset;
        graph.step();
        good &= graph.result[0] == current && graph.last[0] == last &&
            (uint64_t(graph.audit[0]) | (uint64_t(graph.audit[1]) << 32)) == expected_audit &&
            graph.committed_copy[0] == (current ^ 0xe7);
#endif
#ifdef REG_RTL
        rtl.clk = 0;
        rtl.eval();
        good &= rtl.result_out == current && rtl.last_out == last && rtl.audit_out == expected_audit &&
            rtl.committed_copy_out == (current ^ 0xe7);
#endif
        if (!good) {
            std::fprintf(stderr, "reg copy mismatch: lanes=%u control=%u sample=%u input=%u\n",
                         COPY_LANES, COPY_CONTROL, sample, input);
            return 1;
        }
        previous = current;
    }
    std::printf("local reg copy: lanes=%u control=%u, 4096 clocked samples passed\n", COPY_LANES, COPY_CONTROL);
}
