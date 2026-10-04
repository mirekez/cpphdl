#include <cpphdl.h>

// Wide dynamic writes must retain storage identity: mutating current register
// state directly remains forbidden even though pending-state writes work.
class WideSliceWriteReject : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<7>) first_in;
    cpphdl::reg<cpphdl::logic<128>> state;

    void _work(bool) {
        state._next = state;
        state.bits(unsigned(first_in()) + 31, unsigned(first_in())) = 1;
    }
    void _strobe() { state.strobe(); }
};

extern WideSliceWriteReject cpphdl_top;
