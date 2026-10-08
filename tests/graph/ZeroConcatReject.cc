#include "cpphdl.h"
using namespace cpphdl;

class ZeroConcatReject : public Module {
public:
#if ZERO_REJECT == 0
    _PORT(logic<0>) data_in;
#elif ZERO_REJECT == 1
    reg<logic<0>> state;
    void _work(bool) { state._next = 0; }
    void _strobe() { state.strobe(); }
#elif ZERO_REJECT == 2
    logic<0> storage;
    _PORT(logic<8>) result_out = _ASSIGN(cat{storage, logic<8>(1)});
#elif ZERO_REJECT == 3
    _PORT(logic<8>) result_out = _ASSIGN(result());
    logic<8> result() {
        logic<0> storage = 0;
        return cat{storage, logic<8>(1)};
    }
#elif ZERO_REJECT == 4
    _PORT(logic<8>) result_out = _ASSIGN(cat{logic<0>(0), logic<0>(0)});
#elif ZERO_REJECT == 5
    _PORT(logic<8>) result_out = _ASSIGN(repeat<0>(logic<1>(1)));
#elif ZERO_REJECT == 6
    array<1, logic<0>> storage;
    _PORT(logic<8>) result_out = _ASSIGN(cat{storage[0], logic<8>(1)});
#elif ZERO_REJECT == 7
    _PORT(logic<0>) result_out = _ASSIGN(logic<0>(0));
#endif
};
ZeroConcatReject cpphdl_top;
