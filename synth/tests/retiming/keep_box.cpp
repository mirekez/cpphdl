#include "cpphdl.h"
#ifndef BOX_DELAY_NS
#define BOX_DELAY_NS "2.1"
#endif

class [[clang::annotate("CPPHDL_KEEP_BOX=" BOX_DELAY_NS)]] MultiplyBox : public cpphdl::Module {
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint16_t) product_out = _ASSIGN(product_comb_func());
    uint16_t product_comb_func() {
        uint16_t product;
        product = uint16_t(a_in()) * uint16_t(b_in());
        return uint16_t(product + 3);
    }
};

class KeepBoxTest : public cpphdl::Module {
    MultiplyBox multiply;
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint8_t) tag_value_in;
    _PORT(uint16_t) result_out = _ASSIGN_REG(result);
    _PORT(uint8_t) tag_out = _ASSIGN_REG(tag);
    cpphdl::reg<cpphdl::logic<16>> result;
    cpphdl::reg<cpphdl::logic<8>> tag;
    void _assign() {
        multiply.a_in = _ASSIGN(uint8_t(a_in() + 7));
        multiply.b_in = _ASSIGN(b_in());
    }
    void _work(bool reset) {
        uint16_t x;
        x = multiply.product_out() ^ 0x1379;
        result._next = uint16_t(x + 23);
        tag._next = tag_value_in();
        if (reset) { result._next = 0; tag._next = 0x5a; }
    }
    void _strobe() { result.strobe(); tag.strobe(); }
};
KeepBoxTest cpphdl_top;

#ifdef RETIMING_RUN
#include <cstdio>
#include <vector>
#ifdef RETIMING_GRAPH
#include "model.h"
#define DRIVE(name, value) dut.name[0] = value
#define GET(name) dut.name[0]
#else
#include "VSynthRetiming.h"
#define DRIVE(name, value) dut.name = value
#define GET(name) dut.name
#endif
long _system_clock = 0;
int main() {
#ifdef RETIMING_GRAPH
    cpphdl_native::Model dut;
#else
    VSynthRetiming dut;
#endif
    uint8_t a = 0, b = 0, tag = 0;
    std::vector<unsigned> expected, tags;
    unsigned age = 0;
    cpphdl_top.a_in = _ASSIGN(a); cpphdl_top.b_in = _ASSIGN(b);
    cpphdl_top.tag_value_in = _ASSIGN(tag); cpphdl_top._assign();
    for (unsigned cycle = 0; cycle < 4096; ++cycle) {
        bool reset = cycle % 113 == 0;
        a = uint8_t(cycle * 37); b = uint8_t(cycle * 11 + 1); tag = uint8_t(cycle);
        unsigned product = uint8_t(a + 7) * unsigned(b) + 3;
        unsigned result = reset ? 0 : uint16_t((uint16_t(product) ^ 0x1379) + 23);
        expected.push_back(result); tags.push_back(reset ? 0x5a : tag);
        age = reset ? 0 : age + 1;
        cpphdl_top._work(reset); cpphdl_top._strobe();
        if (unsigned(cpphdl_top.result_out()) != result || unsigned(cpphdl_top.tag_out()) != tags.back()) return 1;
        DRIVE(a, a); DRIVE(b, b); DRIVE(tag_value, tag); DRIVE(work_reset, reset);
#ifdef RETIMING_GRAPH
        dut.step();
#else
        dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#endif
        if (!RETIMING_LATENCY || age > RETIMING_LATENCY + 2) {
            auto sample = cycle - RETIMING_LATENCY;
            if (GET(result) != expected[sample] || GET(tag) != tags[sample]) {
                std::fprintf(stderr, "keep box mismatch at %u: %u/%u tag %u/%u\n", cycle,
                    unsigned(GET(result)), expected[sample], unsigned(GET(tag)), tags[sample]);
                return 1;
            }
        }
        if (reset && (GET(result) != 0 || GET(tag) != 0x5a)) return 1;
        ++_system_clock;
    }
    std::puts("4096 kept multiplier transactions: values, aligned tags and resets passed");
}
#endif
