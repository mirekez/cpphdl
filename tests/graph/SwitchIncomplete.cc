#include "cpphdl.h"
class SwitchIncomplete : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) selector_in;
    _PORT(cpphdl::logic<1>) flag_in;
    _PORT(cpphdl::logic<8>) result_out;
    cpphdl::logic<8> next;
    void _assign() { result_out = _ASSIGN(result()); }
    cpphdl::logic<8> result() {
        switch (uint64_t(selector_in())) {
        case 0: next = 7; break;
        case 1: next = 8; break;
#if INCOMPLETE == 1
        default: break;
#elif INCOMPLETE == 2
        default: if (flag_in()) next = 9; break;
#elif INCOMPLETE == 3
        default: next.bits(3, 0) = 9; break;
#elif INCOMPLETE == 4
        default: next = uint64_t(next) + 1; break;
#endif
        }
        return next;
    }
};
extern SwitchIncomplete cpphdl_top;
