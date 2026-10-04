#include "cpphdl.h"

class MulticlockMemory : public cpphdl::Module {
public:
    _PORT(bool) write_enable_in;
    _PORT(bool) read_enable_in;
    _PORT(uint8_t) write_addr_in;
    _PORT(uint8_t) read_addr_in;
    _PORT(uint16_t) data_in;
    _PORT(uint16_t) read_out = _ASSIGN_REG(read_reg);
    _PORT(uint16_t) pending_out = _ASSIGN_REG(pending_reg);
    cpphdl::memory<cpphdl::logic<16>, 1, 8> storage;
    cpphdl::reg<cpphdl::logic<16>> read_reg, pending_reg;

    void _work_write_clk(bool reset) {
        pending_reg._next = pending_reg;
        if (write_enable_in()) {
            storage[write_addr_in()] = data_in();
            // Two same-address writes in one process retain source order.
            storage[write_addr_in()] = uint16_t(data_in() ^ 0x5a5a);
            pending_reg._next = storage.pending(write_addr_in()).to_ullong();
        }
        if (reset) pending_reg._next = 0;
    }
    void _strobe_write_clk() {
        pending_reg.strobe();
#if SYNTH_MEMORY_ERROR != 1 && SYNTH_MEMORY_ERROR != 2
        storage.apply();
#endif
    }
    void _work_read_clk(bool reset) {
        read_reg._next = read_reg;
        if (read_enable_in()) {
#if SYNTH_MEMORY_ERROR == 4
            read_reg._next = storage.pending(read_addr_in()).to_ullong();
#else
            read_reg._next = storage[read_addr_in()].to_ullong();
#endif
        }
#if SYNTH_MEMORY_ERROR == 3
        storage[read_addr_in()] = data_in();
#endif
        if (reset) read_reg._next = 0;
    }
    void _strobe_read_clk() {
        read_reg.strobe();
#if SYNTH_MEMORY_ERROR == 2
        storage.apply();
#endif
    }
#ifdef SYNTH_MEMORY_ASYNC
    void _reset_pos_write_clk() { pending_reg.clr(); }
    void _reset_pos_read_clk() { read_reg.clr(); }
#endif
};
MulticlockMemory cpphdl_top;

#ifdef SYNTH_MULTICLOCK_RUN
#include "Check.h"
#include <array>
#ifdef SYNTH_MULTICLOCK_GRAPH
#include "model.h"
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
#include "VMulticlockMemory.h"
#endif
long _system_clock = 0;
int main() {
    bool we = false, re = false, wc = false, rc = false, previous_reset = false;
    uint8_t wa = 0, ra = 0;
    uint16_t data = 0, expected = 0, pending = 0;
    std::array<uint16_t, 8> memory{};
    unsigned initialized = 0, collisions = 0, writes = 0, reads = 0;
#ifdef SYNTH_MULTICLOCK_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
    VMulticlockMemory dut;
#endif
    cpphdl_top.write_enable_in = _ASSIGN(we);
    cpphdl_top.read_enable_in = _ASSIGN(re);
    cpphdl_top.write_addr_in = _ASSIGN(wa);
    cpphdl_top.read_addr_in = _ASSIGN(ra);
    cpphdl_top.data_in = _ASSIGN(data);
    for (unsigned t = 1; t < 6000; ++t) {
        bool nw = t >= 500 && t < 700 ? wc : (t / 2) % 2;
        bool nr = t >= 1200 && t < 1400 ? rc : (t / 6) % 2;
        bool write_edge = nw && !wc, read_edge = nr && !rc;
        bool reset = t < 25;
#ifdef SYNTH_MEMORY_ASYNC
        reset = reset || (t >= 551 && t < 558) || (t >= 1251 && t < 1260);
#endif
        bool asserted = reset && !previous_reset;
        wa = initialized < 8 ? initialized : (t / 4) % 8;
        ra = wa;
        data = uint16_t(t * 317);
        we = !reset && (initialized < 8 || t % 7 != 0);
        re = !reset && initialized == 8 && t % 5 != 0;
#ifdef SYNTH_MEMORY_ASYNC
        if (initialized == 8) we = t % 7 != 0;
#endif
        // Invalid addresses while the owner clock is stopped must not execute.
        if (t >= 520 && t < 680) wa = 255;
        if (t >= 1220 && t < 1380) ra = 255;
        if (read_edge && re) { expected = memory[ra]; ++reads; }
        if (write_edge && we && !reset) {
            memory[wa] = data ^ 0x5a5a;
            pending = memory[wa]; ++writes;
            if (initialized < 8) ++initialized;
        }
        if (reset && write_edge) pending = 0;
        if (reset && read_edge) expected = 0;
#ifdef SYNTH_MEMORY_ASYNC
        if (reset) { pending = 0; expected = 0; }
        if (reset) {
            if (asserted || write_edge) cpphdl_top._reset_pos_write_clk();
            if (asserted || read_edge) cpphdl_top._reset_pos_read_clk();
        } else
#endif
        {
            collisions += read_edge && write_edge && we && re && wa == ra;
            if ((t / 12) & 1) {
                if (read_edge) cpphdl_top._work_read_clk(reset);
                if (write_edge) cpphdl_top._work_write_clk(reset);
            } else {
                if (write_edge) cpphdl_top._work_write_clk(reset);
                if (read_edge) cpphdl_top._work_read_clk(reset);
            }
        }
#ifdef SYNTH_MEMORY_ASYNC
        if (asserted) { cpphdl_top._strobe_write_clk(); cpphdl_top._strobe_read_clk(); }
#endif
        if (write_edge) cpphdl_top._strobe_write_clk();
        if (read_edge) cpphdl_top._strobe_read_clk();
        wc = nw; rc = nr; previous_reset = reset;
#ifdef SYNTH_MULTICLOCK_GRAPH
        dut.write_clk = wc; dut.read_clk = rc; dut.work_reset[0] = reset;
        dut.write_enable[0] = we; dut.read_enable[0] = re;
        dut.write_addr[0] = wa; dut.read_addr[0] = ra; dut.data[0] = data;
        dut.eval(); dut.step(); dut.step();
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
        dut.write_clk = wc; dut.read_clk = rc; dut.work_reset = reset;
        dut.write_enable = we; dut.read_enable = re;
        dut.write_addr = wa; dut.read_addr = ra; dut.data = data;
        dut.eval(); dut.eval();
#endif
        if (t >= 24) {
            checkClockValue("read C++", cpphdl_top.read_out(), expected, t);
            checkClockValue("pending C++", cpphdl_top.pending_out(), pending, t);
#ifdef SYNTH_MULTICLOCK_GRAPH
            checkClockValue("read graph", dut.read[0], expected, t);
            checkClockValue("pending graph", dut.pending[0], pending, t);
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
            checkClockValue("read gates", dut.read, expected, t);
            checkClockValue("pending gates", dut.pending, pending, t);
#endif
        }
        ++_system_clock;
    }
    if (!collisions || !reads || !writes) return 1;
    std::puts("multiclock memory: old-data collisions, ordered writes, forwarding, enables and stopped clocks passed");
}
#endif
