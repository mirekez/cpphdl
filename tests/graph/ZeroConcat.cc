#include <cpphdl.h>
using namespace cpphdl;

class ZeroConcat : public Module {
public:
    _PORT(logic<128>) data_in;
    _PORT(uint32_t) selector_in;
    _PORT(logic<64>) result_out;
    logic<64> calculate() {
        logic<8> value = logic<8>(data_in().bits(7, 0));
        return cat(logic<0>(0), value, repeat<0, 8>(value),
                   logic<8>(selector_in()), logic<0>(0));
    }
    void _assign() { result_out = _ASSIGN(calculate()); }
};
extern ZeroConcat cpphdl_top;
