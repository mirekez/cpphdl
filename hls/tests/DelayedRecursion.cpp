#include "../Clocked.h"
#include "DelayedOptions.h"
#include <array>

struct RecursionMethods {
    std::array<uint32_t, 2> values{};
    uint64_t outer(uint32_t n) {
        if (n == 0) return 7;
        if (n == 1) return inner(0) + 3;
        uint64_t first = outer(n - 1);
        return first + inner(n - 1);
    }
    uint64_t inner(uint32_t n) {
        if (n == 0) return 5;
        uint64_t first = outer(n - 1);
        return first + inner(n - 1);
    }
    uint64_t descend(uint32_t depth, uint32_t value) {
        if (depth == 0) return value;
        uint64_t sum = 0;
        for (uint32_t i = 0; i < 2; ++i) sum += value + i;
        return sum + descend(depth - 1, value + 1);
    }
    uint64_t branching(uint32_t depth, uint32_t value) {
        values[1] += value;
        if (depth == 0) return values[1];
        uint64_t saved = value * 17 + values[1];
        uint64_t left = branching(depth - 1, value + 1);
        uint64_t right = branching(depth - 1, value + 3);
        return saved + left * 131 + right;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 99) return outer(index);
        values[0] = value;
        values[1] = operation;
        values[index & 1] += 1;
        uint64_t first = branching(index, value);
        uint64_t second = branching(index, value + 7);
        return first * 65537 + second + descend(index, values[0]) + values[1];
    }
};

class DelayedRecursionTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<RecursionMethods, 4, 64, 4096, HLS_SHARED_MEMORY, HLS_BLOCK_RAM> worker;
    cpphdl::hls::ClockedDelayer<RecursionMethods, 2, 64, 4096, HLS_SHARED_MEMORY, HLS_BLOCK_RAM> narrow;
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
#include "VDelayedModel.h"
#endif
long _system_clock = 0;
int main() {
    try {
        RecursionMethods reference;
        auto check = [](bool condition) { if (!condition) throw std::runtime_error("bounded recursion regression failed"); };
#ifdef VERILATOR
        VDelayedModel dut;
        auto tick = [&] { dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); };
        auto reset = [&] {
            dut.command_valid_in = 0; dut.response_ready_in = 0;
            dut.reset = 1; tick(); dut.reset = 0;
            for (unsigned i = 0; !dut.command_ready_out && i < 50; ++i) tick();
            check(dut.command_ready_out && !dut.response_valid_out && !dut.fault_out && !dut.narrow_fault_out);
        };
        reset();
        auto transaction = [&](unsigned depth, unsigned expectedFault, unsigned operation = 7) {
            check(dut.command_ready_out);
            dut.operation_in = operation; dut.index_in = depth; dut.value_in = 19;
            dut.command_valid_in = 1; tick(); dut.command_valid_in = 0;
            dut.index_in = 99; dut.value_in = 99;
            // Two branching traversals now include serialized memory accesses.
            for (unsigned i = 0; !dut.response_valid_out && i < 1000; ++i) tick();
            check(dut.response_valid_out && dut.fault_out == expectedFault);
            if (!expectedFault) check(dut.result_out == reference.command(operation, depth, 19));
            if (depth < (operation == 99 ? 3u : 2u)) check(dut.narrow_valid_out && dut.narrow_fault_out == 0 && dut.narrow_result_out == dut.result_out);
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
        reset();
        for (unsigned i = 0; i <= 4; ++i) transaction(i, 0, 99);
        transaction(5, 5, 99);
        reset();
        transaction(2, 0, 99);
#else
        DelayedRecursionTop dut;
        bool valid = false, ready = false;
        uint32_t depth = 0;
        uint32_t operation = 7;
        dut.command_valid_in = _ASSIGN(valid);
        dut.response_ready_in = _ASSIGN(ready);
        dut.operation_in = _ASSIGN(operation);
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
        operation = 99;
        for (depth = 0; depth <= 4; ++depth) {
            valid = true; tick(); valid = false;
            check(dut.response_valid_out() && dut.result_out() == reference.command(operation, depth, 19));
            ready = true; tick(); ready = false;
        }
#endif
        std::puts("bounded recursive calls and depth-specific values passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
#endif
