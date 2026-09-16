#pragma once
#include <cstdio>
#include <stdexcept>
#ifdef VERILATOR
#include "VClockedModel.h"
#endif

long _system_clock = 0;

template<class T, class Top> int clockedTest()
{
    try {
        T reference{};
        unsigned transactions = 0;
#ifdef VERILATOR
        unsigned commandClocks = 0, longestCommand = 0;
#endif
#ifdef VERILATOR
        VClockedModel dut;
        auto tick = [&] { dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); };
        dut.command_valid_in = 0; dut.response_ready_in = 0;
        dut.operation_in = 0; dut.index_in = 0; dut.value_in = 0;
        dut.reset = 1; tick(); dut.reset = 0;
        for (unsigned i = 0; !dut.command_ready_out && i < 1000; ++i) tick();
        if (!dut.command_ready_out) throw std::runtime_error("reset constructor did not complete");
#else
        Top dut;
        bool valid = false, ready = false;
        uint32_t operation = 0, index = 0, value = 0;
        dut.command_valid_in = _ASSIGN(valid);
        dut.response_ready_in = _ASSIGN(ready);
        dut.operation_in = _ASSIGN(operation);
        dut.index_in = _ASSIGN(index);
        dut.value_in = _ASSIGN(value);
        dut._assign();
        auto tick = [&] { dut._work(false); dut._strobe(); ++_system_clock; };
        dut._work(true); dut._strobe(); ++_system_clock;
#endif
        auto transaction = [&](uint32_t op, uint32_t idx, uint32_t val) {
            ++transactions;
            uint64_t expected = reference.command(op, idx, val);
#ifdef VERILATOR
            if (!dut.command_ready_out) throw std::runtime_error("not ready for command");
            dut.operation_in = op; dut.index_in = idx; dut.value_in = val;
            dut.command_valid_in = 1; tick(); dut.command_valid_in = 0;
            unsigned clocks = 1;
            // Change pins while the call is suspended: arguments must be saved.
            dut.operation_in = 99; dut.index_in = 99; dut.value_in = 99;
            for (; !dut.response_valid_out && clocks < 1000; ++clocks) tick();
            if (dut.fault_out || !dut.response_valid_out || dut.result_out != expected) {
                std::fprintf(stderr, "op=%u idx=%u value=%u expected=%llu actual=%llu fault=%u clocks=%u\n", op, idx, val,
                    (unsigned long long)expected, (unsigned long long)dut.result_out, dut.fault_out, clocks);
                throw std::runtime_error("AST scheduled result mismatch");
            }
            if (T::singleClock(op) && clocks != 1) throw std::runtime_error("straight-line call was split across clocks");
            if (op == 1 && clocks <= 1) throw std::runtime_error("loops were not clocked");
            commandClocks += clocks;
            if (clocks > longestCommand) longestCommand = clocks;
            for (unsigned i = 0; i < 3; ++i) {
                tick();
                if (!dut.response_valid_out || dut.result_out != expected || dut.command_ready_out)
                    throw std::runtime_error("response changed under backpressure");
            }
            dut.response_ready_in = 1; tick(); dut.response_ready_in = 0;
#else
            operation = op; index = idx; value = val; valid = true; tick(); valid = false;
            if (!dut.response_valid_out() || dut.result_out() != expected) throw std::runtime_error("native transaction mismatch");
            ready = true; tick(); ready = false;
#endif
        };
        for (unsigned i = 0; i < 8; ++i) transaction(0, i, i * 7 + 3);
        transaction(1, 17, 29);
        transaction(2, 0, 42);
        transaction(1, 31, 43);
        for (unsigned i = 0; i < 8; ++i) transaction(3, i, 0);
        T::extraTests(transaction);
#ifdef VERILATOR
        dut.operation_in = 1; dut.index_in = 3; dut.value_in = 7;
        dut.command_valid_in = 1; tick(); dut.command_valid_in = 0; tick();
        if (dut.response_valid_out) throw std::runtime_error("loop completed before reset cancellation test");
        dut.reset = 1; tick(); dut.reset = 0;
        for (unsigned i = 0; !dut.command_ready_out && i < 1000; ++i) tick();
        if (!dut.command_ready_out || dut.response_valid_out || dut.fault_out)
            throw std::runtime_error("reset did not cancel the suspended command");
#else
        dut._work(true); dut._strobe(); ++_system_clock;
#endif
        reference = T{};
        for (unsigned i = 0; i < 8; ++i) transaction(0, i, i + 1);
        transaction(1, 13, 23);
#ifdef VERILATOR
        std::printf("%u commands: %u execution clocks, longest command %u clocks\n", transactions, commandClocks, longestCommand);
#else
        std::printf("%u native reference commands\n", transactions);
#endif
        std::puts("clocked container transaction, loop, and backpressure checks passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
