#include <cpphdl.h>
using namespace cpphdl;

class EmptyRepeatCastReject : public Module {
public:
    _PORT(logic<64>) result_out;
    _PORT(logic<1>) trigger_in;
#if EMPTY_REJECT == 0
    _PORT(logic<0>) source_in;
#elif EMPTY_REJECT == 1
    logic<0> source;
#elif EMPTY_REJECT == 2
    reg<logic<0>> source;
#endif
    uint64_t result() {
#if EMPTY_REJECT == 0
        return uint64_t(source_in());
#elif EMPTY_REJECT == 1 || EMPTY_REJECT == 2
        return uint64_t(source);
#elif EMPTY_REJECT == 3
        logic<0> local = 0;
        return uint64_t(local);
#else
        // Keep the constructor visible to lowering rather than letting Clang
        // fold an entirely constant C++ expression to the scalar zero.
        return uint64_t(cat{logic<0>(uint64_t(trigger_in())), logic<0>(0)});
#endif
    }
    void _assign() { result_out = _ASSIGN(result()); }
};
extern EmptyRepeatCastReject cpphdl_top;
