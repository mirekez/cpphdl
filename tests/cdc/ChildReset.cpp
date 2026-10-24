#include <cpphdl.h>
using namespace cpphdl;

class ResetCounter : public Module {
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

class ChildReset : public Module {
public:
    _PORT(bool) clear_in;
    _PORT(u<4>) clear_mask_in;
    _PORT(u<8>) scalar_out = _ASSIGN(scalar.value_out());
    _PORT(u<8>) array_out[4];
private:
    ResetCounter scalar;
    ResetCounter counters[4];
public:
    void _assign() {
        scalar._assign();
        for (unsigned i = 0; i < 4; ++i) {
            counters[i]._assign();
            array_out[i] = _ASSIGN_I(counters[i].value_out());
        }
    }
    void _work(bool reset) {
#ifdef CHILD_RESET_LOCAL
        bool local_reset;
        local_reset = reset || clear_in();
        scalar._work(local_reset);
#else
        scalar._work(reset || clear_in());
#endif
#ifdef CHILD_RESET_CONFLICT
        scalar._work(reset || !clear_in());
#endif
        for (unsigned i = 0; i < 4; ++i)
            counters[i]._work(reset || ((unsigned(clear_mask_in()) >> i) & 1));
    }
    void _strobe() {
        scalar._strobe();
        for (unsigned i = 0; i < 4; ++i)
            counters[i]._strobe();
    }
};
