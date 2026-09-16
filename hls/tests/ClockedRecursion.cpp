#include "../Clocked.h"
#include <array>

struct RecursionMethods {
    std::array<uint32_t, 2> values{};
    uint64_t descend(uint32_t depth, uint32_t value) {
        if (depth == 0) return value;
        uint64_t sum = 0;
        for (uint32_t i = 0; i < 2; ++i) sum += value + i;
        return sum + descend(depth - 1, value + 1);
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        values[0] = value;
        values[1] = operation;
        return descend(index, values[0]) + values[1];
    }
};

class ClockedRecursionTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<RecursionMethods, 4> worker;
    cpphdl::hls::Clocked<RecursionMethods, 2> narrow;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    _PORT(uint32_t) narrow_fault_out;
    _PORT(uint64_t) narrow_result_out;
    _PORT(bool) narrow_valid_out;
    void _assign() {
        worker.command_valid_in = _ASSIGN(command_valid_in());
        worker.operation_in = _ASSIGN(operation_in());
        worker.index_in = _ASSIGN(index_in());
        worker.value_in = _ASSIGN(value_in());
        worker.response_ready_in = _ASSIGN(response_ready_in());
        worker._assign();
        narrow.command_valid_in = _ASSIGN(command_valid_in());
        narrow.operation_in = _ASSIGN(operation_in());
        narrow.index_in = _ASSIGN(index_in());
        narrow.value_in = _ASSIGN(value_in());
        narrow.response_ready_in = _ASSIGN(response_ready_in());
        narrow._assign();
        narrow_fault_out = _ASSIGN(narrow.fault_out());
        narrow_result_out = _ASSIGN(narrow.result_out());
        narrow_valid_out = _ASSIGN(narrow.response_valid_out());
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); narrow._work(reset); }
    void _strobe() { worker._strobe(); narrow._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
#include <stdexcept>
#ifdef VERILATOR
#include "VClockedModel.h"
#endif
long _system_clock = 0;
int main() {
    try {
        RecursionMethods reference;
        auto check = [](bool condition) { if (!condition) throw std::runtime_error("bounded recursion regression failed"); };
#ifdef VERILATOR
        VClockedModel dut;
        auto tick = [&] { dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); };
        auto reset = [&] {
            dut.command_valid_in = 0; dut.response_ready_in = 0;
            dut.reset = 1; tick(); dut.reset = 0;
            for (unsigned i = 0; !dut.command_ready_out && i < 50; ++i) tick();
            check(dut.command_ready_out && !dut.response_valid_out && !dut.fault_out && !dut.narrow_fault_out);
        };
        reset();
        auto transaction = [&](unsigned depth, unsigned expectedFault) {
            check(dut.command_ready_out);
            dut.operation_in = 7; dut.index_in = depth; dut.value_in = 19;
            dut.command_valid_in = 1; tick(); dut.command_valid_in = 0;
            dut.index_in = 99; dut.value_in = 99;
            for (unsigned i = 0; !dut.response_valid_out && i < 100; ++i) tick();
            check(dut.response_valid_out && dut.fault_out == expectedFault);
            if (!expectedFault) check(dut.result_out == reference.command(7, depth, 19));
            if (depth < 2) check(dut.narrow_valid_out && dut.narrow_fault_out == 0 && dut.narrow_result_out == dut.result_out);
            else check(dut.narrow_fault_out == 5);
            auto result = dut.result_out;
            for (unsigned i = 0; i < 3; ++i) {
                tick();
                check(dut.response_valid_out && !dut.command_ready_out && dut.result_out == result && dut.fault_out == expectedFault);
            }
            dut.response_ready_in = 1; tick(); dut.response_ready_in = 0;
            if (expectedFault) check(!dut.command_ready_out && !dut.response_valid_out);
        };
        for (unsigned i = 0; i <= 3; ++i) transaction(i, 0);
        transaction(4, 5);
        reset();
        transaction(3, 0);
#else
        ClockedRecursionTop dut;
        bool valid = false, ready = false;
        uint32_t depth = 0;
        dut.command_valid_in = _ASSIGN(valid);
        dut.response_ready_in = _ASSIGN(ready);
        dut.operation_in = _ASSIGN(7u);
        dut.index_in = _ASSIGN(depth);
        dut.value_in = _ASSIGN(19u);
        dut._assign();
        dut._work(true); dut._strobe(); ++_system_clock;
        auto tick = [&] { dut._work(false); dut._strobe(); ++_system_clock; };
        for (depth = 0; depth <= 3; ++depth) {
            valid = true; tick(); valid = false;
            check(dut.response_valid_out() && dut.result_out() == reference.command(7, depth, 19));
            ready = true; tick(); ready = false;
        }
#endif
        std::puts("bounded recursive calls and depth-specific values passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
#endif
