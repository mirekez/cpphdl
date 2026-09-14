#pragma once
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>

struct AllocationFault : std::bad_alloc { uint32_t code; explicit AllocationFault(uint32_t c) : code(c) {} };
extern "C" [[noreturn]] void cpphdl_hls_fault(uint32_t code) { throw AllocationFault(code); }

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

#ifdef VERILATOR
#include "VScheduledKernel.h"
#include "../Memory.h"
#include <array>
long _system_clock = 0;

// Testbench adapter: only the public interface crosses the model boundary.
class ScheduledModel : public cpphdl::Module {
public:
    HlsMemoryIf memory_out;
    VScheduledKernel rtl;
    void _assign() {
        memory_out.valid_in = _ASSIGN((bool)rtl.memory_out___05Fvalid_out);
        memory_out.write_in = _ASSIGN((bool)rtl.memory_out___05Fwrite_out);
        memory_out.addr_in = _ASSIGN((uint32_t)rtl.memory_out___05Faddr_out);
        memory_out.data_in = _ASSIGN((uint64_t)rtl.memory_out___05Fdata_out);
        memory_out.ready_in = _ASSIGN((bool)rtl.memory_out___05Fready_out);
    }
    void settle(bool reset) {
        rtl.clk = 0; rtl.reset = reset;
        rtl.memory_out___05Fready_in = memory_out.ready_out();
        rtl.memory_out___05Fvalid_in = memory_out.valid_out();
        rtl.memory_out___05Fdata_in = memory_out.data_out();
        rtl.eval();
    }
};

class DelayedMemory : public cpphdl::Module {
    std::array<uint64_t, 128> words;
    unsigned phase = 0, next_phase = 0, delay = 0, next_delay = 0;
    uint32_t address = 0;
    uint64_t data = 0, response = 0;
    bool store = false, request = false;
public:
    HlsMemoryIf memory_in;
    uint64_t transactions = 0;
    DelayedMemory() { words.fill(0xa5c39e1726384bdfULL); }
    void _assign() {
        memory_in.ready_out = _ASSIGN(phase == 0 && _system_clock % 5 != 0);
        memory_in.valid_out = _ASSIGN(phase == 2);
        memory_in.data_out = _ASSIGN_REG(response);
    }
    void _work(bool reset) {
        next_phase = phase; next_delay = delay; request = false;
        if (reset) next_phase = 0;
        else if (phase == 0 && memory_in.ready_out() && memory_in.valid_in()) {
            address = memory_in.addr_in(); data = memory_in.data_in(); store = memory_in.write_in();
            require(address < words.size(), "SRAM address outside bounded arena");
            request = true; next_phase = 1; next_delay = transactions % 4;
        } else if (phase == 1) {
            if (delay) --next_delay; else next_phase = 2;
        } else if (phase == 2 && memory_in.ready_in()) next_phase = 0;
    }
    void _strobe() {
        if (request) {
            if (store) words[address] = data;
            response = store ? 0 : words[address];
            ++transactions;
        }
        phase = next_phase; delay = next_delay;
    }
};

class Bench : public cpphdl::Module {
public:
    ScheduledModel model;
    DelayedMemory ram;
    void _assign() { assignIf(model, ram, model.memory_out, ram.memory_in); }
    void tick(bool reset = false) {
        model.settle(reset);
        bool pending = model.rtl.memory_out___05Fvalid_out && !model.rtl.memory_out___05Fready_in;
        auto address = model.rtl.memory_out___05Faddr_out;
        auto data = model.rtl.memory_out___05Fdata_out;
        auto write = model.rtl.memory_out___05Fwrite_out;
        ram._work(reset);
        model.rtl.clk = 1; model.rtl.eval();
        ram._strobe(); ++_system_clock;
        model.rtl.clk = 0; model.rtl.eval();
        if (pending && !reset) require(model.rtl.memory_out___05Fvalid_out &&
            address == model.rtl.memory_out___05Faddr_out && data == model.rtl.memory_out___05Fdata_out &&
            write == model.rtl.memory_out___05Fwrite_out, "unstable memory request under backpressure");
    }
    void reset() {
        model.rtl.command_valid_in = 0; model.rtl.response_ready_in = 0;
        tick(true); tick(true);
        unsigned clocks = 0;
        while (!model.rtl.command_ready_out) {
            require(++clocks < 100000, "constructor/reset did not finish"); tick();
        }
        require(!model.rtl.fault_out, "constructor fault");
    }
    unsigned transaction(uint32_t op, uint32_t index, uint32_t input, uint64_t expected, uint32_t fault) {
        require(model.rtl.command_ready_out, "command not ready");
        model.rtl.operation_in = op; model.rtl.index_in = index; model.rtl.value_in = input;
        model.rtl.command_valid_in = 1; tick(); model.rtl.command_valid_in = 0;
        model.rtl.operation_in = 99; model.rtl.index_in = ~index; model.rtl.value_in = ~input;
        unsigned clocks = 1; // Include the command acceptance edge.
        while (!model.rtl.response_valid_out) { require(++clocks < 300000, "transaction did not finish"); tick(); }
        if (model.rtl.fault_out != fault || (!fault && model.rtl.result_out != expected)) {
            std::fprintf(stderr, "op=%u index=%u input=%u got=%llu expected=%llu fault=%u expected_fault=%u clocks=%u\n",
                op, index, input, (unsigned long long)model.rtl.result_out, (unsigned long long)expected,
                model.rtl.fault_out, fault, clocks);
            require(false, "scheduled RTL differs from the native method");
        }
        auto result = model.rtl.result_out;
        for (unsigned n = 0; n < 4; ++n) {
            tick(); require(model.rtl.response_valid_out && result == model.rtl.result_out &&
                !model.rtl.command_ready_out, "response not held under backpressure");
        }
        model.rtl.response_ready_in = 1; tick(); model.rtl.response_ready_in = 0;
        require(!model.rtl.response_valid_out, "response not acknowledged");
        if (fault) require(!model.rtl.command_ready_out, "fault must require reset");
        return clocks;
    }
};
#endif
