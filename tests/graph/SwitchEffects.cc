#include <cpphdl.h>
#include <cstdlib>
using namespace cpphdl;

class SwitchEffects : public Module {
public:
    _PORT(uint32_t) selector_in;
    _PORT(uint32_t) result_out;
    reg<logic<32>> result_reg;
    uint32_t choose() {
        switch (selector_in()) {
        case 0: return uint32_t(::random());
        case 1: return 7;
        default: break;
        }
        return uint32_t(::random()) + uint32_t(::random());
    }
    void _assign() { result_out = _ASSIGN(result_reg); }
    void _work(bool reset) { result_reg._next = reset ? 0 : choose(); }
    void _strobe() { result_reg.strobe(); }
};
extern SwitchEffects cpphdl_top;
