#include <cpphdl.h>
using namespace cpphdl;

class ClockCounter : public Module {
public:
    _PORT(u<8>) value_out = _ASSIGN_REG(value);
private:
    reg<u<8>> value{};
public:
    void _assign() {}
    void _work(bool reset) {
        value._next = value + 1;
        if (reset)
            value._next = 0;
    }
    void _strobe() { value.strobe(); }
};

class ChildClock : public Module {
public:
    _PORT(bool) clear_in;
    _PORT(u<8>) fast_out = _ASSIGN(fast.value_out());
    _PORT(u<8>) negative_out = _ASSIGN(negative.value_out());
    _PORT(u<8>) slow_out[2];
private:
    ClockCounter fast, negative, slow[2];
public:
    void _assign() {
        fast._assign();
        negative._assign();
        for (unsigned i = 0; i < 2; ++i) {
            slow[i]._assign();
            slow_out[i] = _ASSIGN_I(slow[i].value_out());
        }
    }
    // Keep legacy local delegation: the caller determines its clock domain.
    void _work(bool reset) { fast._work(reset || clear_in()); }
    void _work_fast_clk(bool reset) {
        _work(reset);
#ifdef CHILD_CLOCK_CONFLICT
        for (unsigned i = 0; i < 2; ++i)
            slow[i]._work(reset || clear_in());
#endif
    }
    void _strobe_fast_clk() { fast._strobe(); }
    void _work_neg_fast_clk(bool reset) { negative._work(reset || clear_in()); }
    void _strobe_neg_fast_clk() { negative._strobe(); }
    void _work_slow_clk(bool reset) {
        for (unsigned i = 0; i < 2; ++i)
            slow[i]._work(reset || clear_in());
    }
    void _strobe_slow_clk() {
        for (unsigned i = 0; i < 2; ++i)
            slow[i]._strobe();
    }
};
