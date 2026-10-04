#include "cpphdl.h"

class ClockedCounter : public cpphdl::Module {
public:
    _PORT(bool) en_in;
    _PORT(cpphdl::logic<8>) count_out = _ASSIGN_REG(count);
    cpphdl::reg<cpphdl::logic<8>> count;
    void _work(bool reset) {
#if !SYNTH_CLOCK_ERROR
        // A local register-wrapper copy is ordinary scratch storage, not a
        // separately clocked register requiring an owner and strobe.
        auto snapshot = count;
        snapshot._next = snapshot;
        if (en_in()) snapshot._next = uint32_t(snapshot) + 1;
        count._next = snapshot._next;
#else
        // Keep the negative ownership controls independent of pending reads.
        count._next = count;
        if (en_in()) count._next = uint32_t(count) + 1;
#endif
        if (reset) count._next = 0;
    }
    void _strobe() { count.strobe(); }
};

class MainAndSecondary : public cpphdl::Module {
    ClockedCounter counter;
public:
    _PORT(bool) en_in;
    _PORT(uint8_t) count_out = _ASSIGN(counter.count_out());
    _PORT(bool) sync1_out = _ASSIGN_REG(sync1);
    _PORT(bool) sync2_out = _ASSIGN_REG(sync2);
    _PORT(uint8_t) falls_out = _ASSIGN_REG(falls);
    cpphdl::reg<cpphdl::logic<1>> sync1, sync2;
    cpphdl::reg<cpphdl::logic<8>> falls;
#if SYNTH_CLOCK_ERROR == 8
    cpphdl::reg<cpphdl::logic<1>> probe;
#endif
    void _assign() { counter.en_in = _ASSIGN(en_in()); }
    void _work_main_clk(bool reset) {
        counter._work(reset);
#if SYNTH_CLOCK_ERROR == 8
        probe._next = sync1._next;
#endif
    }
    void _strobe_main_clk() {
        counter._strobe();
#if SYNTH_CLOCK_ERROR == 7
        sync1.strobe();
#elif SYNTH_CLOCK_ERROR == 8
        probe.strobe();
#endif
    }
    void _work_neg_main_clk(bool reset) {
        falls._next = uint32_t(falls) + 1;
        if (reset) falls._next = 0;
    }
    void _strobe_neg_main_clk() { falls.strobe(); }
    void _work_secondary_clk(bool reset) {
        sync1._next = uint32_t(counter.count_out()) & 1;
        sync2._next = sync1;
        if (reset) { sync1._next = 0; sync2._next = 0; }
#if SYNTH_CLOCK_ERROR == 2
        counter._work(reset);
#elif SYNTH_CLOCK_ERROR == 3
        falls._next = 0;
#elif SYNTH_CLOCK_ERROR == 6
        _work_main_clk(reset);
#endif
    }
#if SYNTH_CLOCK_ERROR != 1
    void _strobe_secondary_clk() {
#if SYNTH_CLOCK_ERROR == 5
        if (en_in()) sync1.strobe();
#elif SYNTH_CLOCK_ERROR != 7
        sync1.strobe();
#endif
        sync2.strobe();
    }
#endif
#if SYNTH_CLOCK_ERROR == 4
    void _work_unknown_clk(bool) { sync1._next = 0; }
#endif
};
MainAndSecondary cpphdl_top;

#ifdef SYNTH_MULTICLOCK_RUN
#include "Check.h"
#ifdef SYNTH_MULTICLOCK_GRAPH
#include "model.h"
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
#include "VMainAndSecondary.h"
#endif
long _system_clock = 0;
int main() {
    bool enable = false, main = false, secondary = false;
    unsigned count = 0, falls = 0, sync1 = 0, sync2 = 0;
    unsigned coincident = 0, stopped = 0;
#ifdef SYNTH_MULTICLOCK_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
    VMainAndSecondary dut;
#endif
    cpphdl_top.en_in = _ASSIGN(enable);
    cpphdl_top._assign();
    for (unsigned t = 1; t < 12000; ++t) {
        bool next_main = (t >= 1000 && t < 1100) ? main : (t / 2) % 2;
        bool next_secondary = (t >= 500 && t < 700) ? secondary : (t / 6) % 2;
        bool rise_main = next_main && !main, fall_main = !next_main && main;
        bool rise_secondary = next_secondary && !secondary;
        bool reset = t < 25 || (t >= 510 && t < 540) || (t >= 2000 && t < 2040);
        enable = t % 7 < 3;
        unsigned old_count = count, old_sync1 = sync1;
        if (rise_main) count = reset ? 0 : ((count + enable) & 255);
        if (fall_main) falls = reset ? 0 : ((falls + 1) & 255);
        if (rise_secondary) { sync1 = reset ? 0 : old_count & 1; sync2 = reset ? 0 : old_sync1; }
        coincident += rise_main && rise_secondary;
        stopped += next_main == main && next_secondary == secondary;
        // Alternate work order; all coincident processes sample old registers.
        if ((t / 12) & 1) {
            if (rise_secondary) cpphdl_top._work_secondary_clk(reset);
            if (rise_main) cpphdl_top._work_main_clk(reset);
        } else {
            if (rise_main) cpphdl_top._work_main_clk(reset);
            if (rise_secondary) cpphdl_top._work_secondary_clk(reset);
        }
        if (fall_main) cpphdl_top._work_neg_main_clk(reset);
        if (rise_secondary) cpphdl_top._strobe_secondary_clk();
        if (rise_main) cpphdl_top._strobe_main_clk();
        if (fall_main) cpphdl_top._strobe_neg_main_clk();
        main = next_main; secondary = next_secondary;
#ifdef SYNTH_MULTICLOCK_GRAPH
        dut.main_clk = main; dut.secondary_clk = secondary;
        dut.en[0] = enable; dut.work_reset[0] = reset;
        dut.eval(); dut.step(); dut.step(); // Repeated levels must not make edges.
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
        dut.main_clk = main; dut.secondary_clk = secondary;
        dut.en = enable; dut.work_reset = reset; dut.eval(); dut.eval();
#endif
        // Allow both edges of each clock to receive the initial synchronous reset.
        if (t >= 24) {
#define CHECK(field, expected) \
            checkClockValue(#field " C++", cpphdl_top.field##_out(), expected, t); CHECK_BACKEND(field, expected)
#ifdef SYNTH_MULTICLOCK_GRAPH
#define CHECK_BACKEND(field, expected) checkClockValue(#field " graph", dut.field[0], expected, t);
#elif defined(SYNTH_MULTICLOCK_VERILATOR)
#define CHECK_BACKEND(field, expected) checkClockValue(#field " gates", dut.field, expected, t);
#else
#define CHECK_BACKEND(field, expected)
#endif
            CHECK(count, count); CHECK(falls, falls); CHECK(sync1, sync1); CHECK(sync2, sync2);
        }
        ++_system_clock;
    }
    if (!coincident || !stopped) return 1;
    std::puts("main/secondary: 11999 timestamps, 3:1 clocks, both edges, hierarchy, reset and stopped clocks passed");
}
#endif
