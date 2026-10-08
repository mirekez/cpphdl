#include "cpphdl.h"

// No definition at all: synthesis must not try to instantiate the body.
#ifndef BLACKBOX_ERROR
#define BLACKBOX_ERROR 0
#endif
#if BLACKBOX_ERROR == 1
[[clang::annotate("CPPHDL_BLACKBOX=opaque_a:-1")]]
#elif BLACKBOX_ERROR == 2
[[clang::annotate("CPPHDL_BLACKBOX=opaque_a:nan")]]
#else
[[clang::annotate("CPPHDL_BLACKBOX=opaque_a:0")]]
#endif
#if BLACKBOX_ERROR == 3
uint16_t opaque_a(const int8_t& a, uint8_t b);
#elif BLACKBOX_ERROR == 4
__uint128_t opaque_a(int8_t a, uint8_t b);
#else
uint16_t opaque_a(int8_t a, uint8_t b);
#endif
[[clang::annotate("CPPHDL_BLACKBOX=opaque_b:0")]]
uint16_t opaque_b(int8_t a, uint8_t b);
[[clang::annotate("CPPHDL_BLACKBOX=opaque_c:0")]]
constexpr uint16_t opaque_c(int8_t a, uint8_t b) { return uint16_t(a * 5 + b); }
constexpr uint16_t wrapped_constant() { return opaque_c(-2, 3); }

class BlackBoxTest : public cpphdl::Module {
public:
    _PORT(int8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint16_t) first_out = _ASSIGN(opaque_a(a_in(), b_in()));
    _PORT(uint16_t) second_out = _ASSIGN(opaque_b(a_in(), b_in()));
    _PORT(uint16_t) constant_out = _ASSIGN(opaque_a(-3, 19));
    _PORT(uint16_t) folded_out = _ASSIGN(uint16_t(wrapped_constant() + 1));
};
BlackBoxTest cpphdl_top;

#ifdef BLACKBOX_RUN
#include "VBlackBoxTest.h"
#include <cstdio>
long _system_clock = 0;
uint16_t opaque_a(int8_t a, uint8_t b) { return uint16_t(a + b); }
uint16_t opaque_b(int8_t a, uint8_t b) { return uint16_t(a - b); }
int main() {
    VBlackBoxTest dut;
    for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; b += 17) {
        dut.a = a; dut.b = b; dut.work_reset = 0; dut.clk = 0; dut.eval();
        if (dut.first != uint16_t(int8_t(a) + b) || dut.second != uint16_t(int8_t(a) - b) || dut.constant != 16 ||
            dut.folded != uint16_t(-6))
            return 1;
    }
    std::puts("PASS: declaration-only blackboxes, signed argument slots, distinct functions and constant invocation");
}
#endif
