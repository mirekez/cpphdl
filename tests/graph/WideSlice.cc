#include <cpphdl.h>
using namespace cpphdl;

class WideSlice : public Module {
public:
    _PORT(logic<128>) data_in;
    _PORT(uint32_t) selector_in;
    _PORT(logic<64>) result_out;
    logic<64> calculate() {
        return logic<64>(data_in().bits(selector_in() + 63, selector_in())) ^
               logic<64>(data_in().bits(95, 32));
    }
    void _assign() { result_out = _ASSIGN(calculate()); }
};
extern WideSlice cpphdl_top;
