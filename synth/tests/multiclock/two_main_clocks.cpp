#include "cpphdl.h"

class TwoMainClocks : public cpphdl::Module {
public:
    _PORT(bool) left_enable_in;
    _PORT(bool) right_enable_in;
    _PORT(bool) left_reset_in;
    _PORT(bool) right_reset_in;
    _PORT(uint8_t) left_count_out = _ASSIGN_REG(left_count);
    _PORT(uint8_t) right_count_out = _ASSIGN_REG(right_count);
    _PORT(bool) left_sync_out = _ASSIGN_REG(left_sync2);
    _PORT(bool) right_sync_out = _ASSIGN_REG(right_sync2);
    cpphdl::reg<cpphdl::logic<8>> left_count, right_count;
    cpphdl::reg<cpphdl::logic<1>> left_sync1, left_sync2, right_sync1, right_sync2;
    void _work_left_clk(bool reset) {
        left_count._next = left_count;
        if (left_enable_in()) left_count._next = uint32_t(left_count) + 1;
        left_sync1._next = uint32_t(right_count) & 1;
        left_sync2._next = left_sync1;
        if (reset || left_reset_in()) {
            left_count._next = 0; left_sync1._next = 0; left_sync2._next = 0;
        }
    }
    void _strobe_left_clk() { left_count.strobe(); left_sync1.strobe(); left_sync2.strobe(); }
    void _work_right_clk(bool reset) {
        right_count._next = right_count;
        if (right_enable_in()) right_count._next = uint32_t(right_count) + 3;
        right_sync1._next = uint32_t(left_count) & 1;
        right_sync2._next = right_sync1;
        if (reset || right_reset_in()) {
            right_count._next = 0; right_sync1._next = 0; right_sync2._next = 0;
        }
    }
    void _strobe_right_clk() { right_count.strobe(); right_sync1.strobe(); right_sync2.strobe(); }
};
TwoMainClocks cpphdl_top;

#ifdef SYNTH_MULTICLOCK_RUN
#include "Check.h"
#ifdef SYNTH_MULTICLOCK_GRAPH
#include "model.h"
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
#include "VTwoMainClocks.h"
#endif
long _system_clock = 0;
int main() {
    bool left_enable = false, right_enable = false, left_reset = false, right_reset = false;
    bool left = false, right = false;
    unsigned lc = 0, rc = 0, l1 = 0, l2 = 0, r1 = 0, r2 = 0, coincident = 0;
#ifdef SYNTH_MULTICLOCK_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
    VTwoMainClocks dut;
#endif
    cpphdl_top.left_enable_in = _ASSIGN(left_enable);
    cpphdl_top.right_enable_in = _ASSIGN(right_enable);
    cpphdl_top.left_reset_in = _ASSIGN(left_reset);
    cpphdl_top.right_reset_in = _ASSIGN(right_reset);
    cpphdl_top._assign();
    for (unsigned t = 1; t < 18000; ++t) {
        bool nl = (t >= 1100 && t < 1300) ? left : ((t + 1) / 3) % 2;
        bool nr = (t >= 2300 && t < 2700) ? right : ((t + 1) / 5) % 2;
        bool le = nl && !left, re = nr && !right;
        bool reset = t < 40;
        left_reset = (t >= 1120 && t < 1180) || (t >= 3300 && t < 3340);
        right_reset = (t >= 2400 && t < 2440) || (t >= 5000 && t < 5040);
        left_enable = t % 11 < 5; right_enable = t % 13 < 7;
        auto old_lc = lc, old_rc = rc, old_l1 = l1, old_r1 = r1;
        if (le) {
            lc = (lc + left_enable) & 255; l1 = old_rc & 1; l2 = old_l1;
            if (reset || left_reset) { lc = 0; l1 = 0; l2 = 0; }
        }
        if (re) {
            rc = (rc + 3 * right_enable) & 255; r1 = old_lc & 1; r2 = old_r1;
            if (reset || right_reset) { rc = 0; r1 = 0; r2 = 0; }
        }
        coincident += le && re;
        if ((t / 30) & 1) {
            if (le) cpphdl_top._work_left_clk(reset);
            if (re) cpphdl_top._work_right_clk(reset);
        } else {
            if (re) cpphdl_top._work_right_clk(reset);
            if (le) cpphdl_top._work_left_clk(reset);
        }
        if (le) cpphdl_top._strobe_left_clk();
        if (re) cpphdl_top._strobe_right_clk();
        left = nl; right = nr;
#ifdef SYNTH_MULTICLOCK_GRAPH
        dut.left_clk = left; dut.right_clk = right;
        dut.left_enable[0] = left_enable; dut.right_enable[0] = right_enable;
        dut.left_reset[0] = left_reset; dut.right_reset[0] = right_reset;
        dut.work_reset[0] = reset; dut.eval(); dut.step(); dut.step();
#endif
#ifdef SYNTH_MULTICLOCK_VERILATOR
        dut.left_clk = left; dut.right_clk = right;
        dut.left_enable = left_enable; dut.right_enable = right_enable;
        dut.left_reset = left_reset; dut.right_reset = right_reset;
        dut.work_reset = reset; dut.eval(); dut.eval();
#endif
        if (t >= 39) {
#define CHECK(field, expected) \
            checkClockValue(#field " C++", cpphdl_top.field##_out(), expected, t); CHECK_BACKEND(field, expected)
#ifdef SYNTH_MULTICLOCK_GRAPH
#define CHECK_BACKEND(field, expected) checkClockValue(#field " graph", dut.field[0], expected, t);
#elif defined(SYNTH_MULTICLOCK_VERILATOR)
#define CHECK_BACKEND(field, expected) checkClockValue(#field " gates", dut.field, expected, t);
#else
#define CHECK_BACKEND(field, expected)
#endif
            CHECK(left_count, lc); CHECK(right_count, rc); CHECK(left_sync, l2); CHECK(right_sync, r2);
        }
        ++_system_clock;
    }
    if (!coincident) return 1;
    std::puts("two main clocks: 17999 timestamps, independent phases, CDC sampling, independent resets and stalls passed");
}
#endif
