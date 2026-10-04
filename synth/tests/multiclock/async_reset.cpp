#include "cpphdl.h"

class ResetCounter : public cpphdl::Module {
public:
    _PORT(uint8_t) count_out = _ASSIGN_REG(count);
    cpphdl::reg<cpphdl::logic<8>> count;
    void _work(bool) { count._next = uint32_t(count) + 1; }
    void _strobe() { count.strobe(); }
    void _reset_pos_fast_clk() { count.clr(); }
};

class AsynchronousReset : public cpphdl::Module {
    ResetCounter counter;
public:
    _PORT(uint8_t) seed_in;
    _PORT(uint8_t) count_out = _ASSIGN(counter.count_out());
    _PORT(uint8_t) fall_out = _ASSIGN_REG(fall_reg);
    _PORT(uint8_t) sample_out = _ASSIGN_REG(sample_reg);
    cpphdl::reg<cpphdl::logic<8>> fall_reg, sample_reg;
#if SYNTH_RESET_ERROR == 4
    cpphdl::memory<cpphdl::logic<8>, 1, 4> memory;
#endif
    void _work_fast_clk(bool reset) { counter._work(reset); }
    void _strobe_fast_clk() { counter._strobe(); }
    void _reset_pos_fast_clk() {
#if SYNTH_RESET_ERROR != 1
        counter._reset_pos_fast_clk();
#endif
#if SYNTH_RESET_ERROR == 3
        sample_reg.clr();
#endif
    }
    void _work_neg_fast_clk(bool) { fall_reg._next = uint32_t(fall_reg) + 3; }
    void _strobe_neg_fast_clk() { fall_reg.strobe(); }
    void _reset_neg_fast_clk() { fall_reg.set(cpphdl::logic<8>(0xa5)); }
    void _work_slow_clk(bool) { sample_reg._next = counter.count_out(); }
    void _strobe_slow_clk() { sample_reg.strobe(); }
    void _reset_pos_slow_clk() {
#if SYNTH_RESET_ERROR == 2
        sample_reg._next = seed_in();
#elif SYNTH_RESET_ERROR == 4
        memory[0] = 0;
#elif SYNTH_RESET_ERROR == 5
        if (seed_in()) sample_reg._next = 0;
#else
        sample_reg._next = 0x3c;
#endif
    }
};
AsynchronousReset cpphdl_top;

#ifdef SYNTH_MULTICLOCK_RUN
#include "Check.h"
#ifdef SYNTH_MULTICLOCK_GRAPH
#include "model.h"
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
#include "VAsynchronousReset.h"
#endif
long _system_clock = 0;
int main() {
    bool fast = false, slow = false, previous_reset = false;
    unsigned count = 0, falls = 0xa5, sample = 0x3c;
    unsigned unclocked_assertions = 0, held_edges = 0, releases = 0;
#ifdef SYNTH_MULTICLOCK_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
    VAsynchronousReset dut;
    dut.fast_clk = 0; dut.slow_clk = 0; dut.work_reset = 0; dut.seed = 17;
    dut.eval(); // Establish low reset before testing its first asynchronous edge.
#endif
    cpphdl_top.seed_in = _ASSIGN(17);
    for (unsigned t = 1; t < 5000; ++t) {
        bool nf = t < 20 || (t >= 1000 && t < 1200) ? fast : (t / 2) % 2;
        bool ns = t < 20 || (t >= 1000 && t < 1200) ? slow : (t / 6) % 2;
        bool rise_fast = nf && !fast, fall_fast = !nf && fast, rise_slow = ns && !slow;
        bool reset = t < 7 || (t >= 1011 && t < 1014) || (t >= 1511 && t < 1543) || t == 2010;
        bool asserted = reset && !previous_reset;
        unsigned old_count = count;
        if (rise_fast) count = (count + 1) & 255;
        if (fall_fast) falls = (falls + 3) & 255;
        if (rise_slow) sample = old_count;
        if (reset) { count = 0; falls = 0xa5; sample = 0x3c; }
        if (reset) {
            if (asserted || rise_fast) cpphdl_top._reset_pos_fast_clk();
            if (asserted || fall_fast) cpphdl_top._reset_neg_fast_clk();
            if (asserted || rise_slow) cpphdl_top._reset_pos_slow_clk();
        } else {
            if (rise_fast) cpphdl_top._work_fast_clk(false);
            if (fall_fast) cpphdl_top._work_neg_fast_clk(false);
            if (rise_slow) cpphdl_top._work_slow_clk(false);
        }
        if (rise_fast || asserted) cpphdl_top._strobe_fast_clk();
        if (fall_fast || asserted) cpphdl_top._strobe_neg_fast_clk();
        if (rise_slow || asserted) cpphdl_top._strobe_slow_clk();
        // Outputs may have been sampled during work. Settle after the commit
        // with a fresh CppHDL comb-cache generation, as the graph backend does.
        ++_system_clock;
        unclocked_assertions += asserted && nf == fast && ns == slow;
        held_edges += reset && previous_reset && (rise_fast || fall_fast || rise_slow);
        releases += !reset && previous_reset && nf == fast && ns == slow;
        fast = nf; slow = ns; previous_reset = reset;
#ifdef SYNTH_MULTICLOCK_GRAPH
        dut.fast_clk = fast; dut.slow_clk = slow; dut.work_reset[0] = reset; dut.seed[0] = 17;
        dut.step(); dut.step();
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
        dut.fast_clk = fast; dut.slow_clk = slow; dut.work_reset = reset; dut.seed = 17;
        dut.eval(); dut.eval();
#endif
        checkClockValue("count C++", cpphdl_top.count_out(), count, t);
        checkClockValue("fall C++", cpphdl_top.fall_out(), falls, t);
        checkClockValue("sample C++", cpphdl_top.sample_out(), sample, t);
#ifdef SYNTH_MULTICLOCK_GRAPH
        checkClockValue("count graph", dut.count[0], count, t);
        checkClockValue("fall graph", dut.fall[0], falls, t);
        checkClockValue("sample graph", dut.sample[0], sample, t);
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
        checkClockValue("count gates", dut.count, count, t);
        checkClockValue("fall gates", dut.fall, falls, t);
        checkClockValue("sample gates", dut.sample, sample, t);
#endif
        ++_system_clock;
    }
    if (!unclocked_assertions || !held_edges || !releases) return 1;
    std::puts("asynchronous reset: stopped clocks, between-edge assertion/release, held reset, hierarchy and both edges passed");
}
#endif
