#include <cstdint>
#include <cstdio>
#include <initializer_list>
#ifdef VERILATOR
#include "VExecuteArithmetic.h"
#else
#include "ExecuteArithmetic.h"
#endif

long _system_clock = 0;

int main()
{
#ifdef VERILATOR
    VExecuteArithmetic dut;
#else
    ExecuteArithmetic dut;
#endif
    uint32_t a = 0, b = 0;
    uint8_t op = 14; // Alu::DIV (DIVU), followed by Alu::REM (REMU).
    bool valid = false;
#ifndef VERILATOR
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    dut.op_in = _ASSIGN(op);
    dut.valid_in = _ASSIGN(valid);
    dut._assign();
#endif
    constexpr uint32_t operands[] = {0, 1, 2, 7, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    unsigned count = 0;
    for (bool reset : {true, false}) {
        for (bool active : {false, true}) {
            valid = active;
            for (uint8_t operation : {14, 15}) {
                op = operation;
                for (uint32_t lhs : operands) {
                    for (uint32_t rhs : operands) {
                        a = lhs;
                        b = rhs;
                        uint32_t expected = op == 14 ? (b ? a / b : UINT32_MAX) : (b ? a % b : a);
                        ++_system_clock;
#ifdef VERILATOR
                        dut.clk = 0;
                        dut.reset = reset;
                        dut.a_in = a;
                        dut.b_in = b;
                        dut.op_in = op;
                        dut.valid_in = valid;
                        dut.eval();
                        uint32_t actual = dut.result_out;
                        dut.clk = 1;
                        dut.eval();
#else
                        uint32_t actual = dut.result_out();
                        dut._work(reset);
                        dut._strobe();
#endif
                        if (actual != expected) {
                            std::fprintf(stderr, "op=%u a=%08x b=%08x valid=%d reset=%d: %08x != %08x\n",
                                         op, a, b, valid, reset, actual, expected);
                            return 1;
                        }
                        ++count;
                    }
                }
            }
        }
    }
    std::printf("PASS %u unsigned divide/remainder cases (including reset and invalid bubbles)\n", count);
}
