#include "cpphdl.h"

class SwitchProbe : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) increment_in, decrement_in;
    _PORT(cpphdl::logic<2>) current_in;
    _PORT(cpphdl::logic<2>) result_out = _ASSIGN_COMB(next_comb_func());
    cpphdl::logic<2> next_comb;

    cpphdl::logic<2>& next_comb_func() {
#ifdef INITIALIZE_FIRST
        next_comb = current_in();
#endif
        switch (uint64_t(cpphdl::cat{increment_in(), decrement_in()})) {
        case 2: next_comb = uint64_t(current_in()) + 1; break;
        case 1: next_comb = uint64_t(current_in()) - 1; break;
        default: next_comb = current_in(); break;
        }
        return next_comb;
    }
};

extern SwitchProbe cpphdl_top;
