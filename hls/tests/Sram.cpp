#include "../Memory.h"

class SramTop : public cpphdl::Module
{
    HlsSram<8> ram;
public:
    HlsMemoryIf memory_in;
    _PORT(bool) enable_in;
    _PORT(uint32_t) delay_in;

    void _assign()
    {
        ram.enable_in = enable_in;
        ram.delay_in = delay_in;
        assignIf(*this, ram, memory_in, ram.memory_in);
    }
    void _work(bool reset) { ram._work(reset); }
    void _strobe() { ram._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
#include <cstdlib>
#ifdef VERILATOR
#include "VSramTop.h"

class SramModel : public cpphdl::Module
{
    VSramTop dut;
public:
    HlsMemoryIf memory_in;
    _PORT(bool) enable_in;
    _PORT(uint32_t) delay_in;
    void _assign()
    {
        memory_in.ready_out = _ASSIGN((bool)dut.memory_in___05Fready_out);
        memory_in.valid_out = _ASSIGN((bool)dut.memory_in___05Fvalid_out);
        memory_in.data_out = _ASSIGN((uint64_t)dut.memory_in___05Fdata_out);
    }
    void settle(bool reset = false)
    {
        dut.clk = 0; dut.reset = reset;
        dut.enable_in = enable_in(); dut.delay_in = delay_in();
        dut.memory_in___05Fvalid_in = memory_in.valid_in();
        dut.memory_in___05Fwrite_in = memory_in.write_in();
        dut.memory_in___05Faddr_in = memory_in.addr_in();
        dut.memory_in___05Fdata_in = memory_in.data_in();
        dut.memory_in___05Fready_in = memory_in.ready_in();
        dut.eval();
    }
    void _work(bool reset)
    {
        settle(reset);
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    }
    void _strobe() {}
};
#else
using SramModel = SramTop;
#endif

long _system_clock = 0;

static void check(bool okay)
{
    if (!okay) {
        std::fprintf(stderr, "SRAM interface mismatch at cycle %ld\n", _system_clock);
        std::exit(1);
    }
}

struct SramBench : public cpphdl::Module
{
    SramModel dut;
    HlsMemoryIf memory_out;
    bool valid = false, write = false, ready = false, enable = true;
    uint32_t address = 0, delay = 0;
    uint64_t data = 0;
    void _assign()
    {
        memory_out.valid_in = _ASSIGN_REG(valid);
        memory_out.write_in = _ASSIGN_REG(write);
        memory_out.addr_in = _ASSIGN_REG(address);
        memory_out.data_in = _ASSIGN_REG(data);
        memory_out.ready_in = _ASSIGN_REG(ready);
        dut.enable_in = _ASSIGN_REG(enable);
        dut.delay_in = _ASSIGN_REG(delay);
        assignIf(*this, dut, memory_out, dut.memory_in);
    }
    void settle()
    {
#ifdef VERILATOR
        dut.settle();
#endif
    }
    void tick(bool reset = false)
    {
        dut._work(reset); dut._strobe(); ++_system_clock;
        settle();
    }
    void transaction(bool store, uint32_t index, uint64_t word)
    {
        unsigned cycles = 0;
        valid = true; write = store; address = index; data = word;
        enable = false; ready = false; delay = index % 4;
        for (unsigned i = 0; i < 3; ++i) {
            tick(); check(!memory_out.ready_out() && !memory_out.valid_out());
        }
        enable = true; tick();
        valid = false; data = ~word; address = 7 - index;
        settle();
        while (!memory_out.valid_out()) { check(++cycles < 10); tick(); }
        for (unsigned i = 0; i < 3; ++i) {
            check(memory_out.valid_out() && !memory_out.ready_out());
            check(memory_out.data_out() == (store ? 0 : word)); tick();
        }
        ready = true; tick(); ready = false;
        check(!memory_out.valid_out());
    }
};

int main()
{
    SramBench bench;
    bench._assign(); bench.tick(true); bench.tick(true);
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < 8; ++i) {
            bench.transaction(true, i, 0xfedcba9876543210ULL ^ (uint64_t(i + pass) << 32) ^ i);
        }
        // Reset clears protocol state, not SRAM contents.
        bench.tick(true);
        for (uint32_t i = 0; i < 8; ++i) {
            bench.transaction(false, i, 0xfedcba9876543210ULL ^ (uint64_t(i + pass) << 32) ^ i);
        }
    }
    std::puts("PASS SRAM: bidirectional interface, full-width data, stalls, reset retention");
}
#endif
