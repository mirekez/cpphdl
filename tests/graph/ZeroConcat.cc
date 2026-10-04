#include "cpphdl.h"
using namespace cpphdl;

class ZeroConcat : public Module {
public:
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) leading_out = _ASSIGN(cat{repeat<0>(logic<1>(1)), data_in()});
    _PORT(logic<32>) trailing_out = _ASSIGN(cat{data_in(), logic<0>(123), repeat<0>(logic<2>(3))});
    _PORT(logic<40>) middle_out = _ASSIGN(cat{logic<8>(0xa5), logic<0>{}, data_in()});
    _PORT(logic<32>) nested_out = _ASSIGN(cat{logic<0>(0), logic<32>(cat{data_in(), logic<0>(0)})});
    _PORT(logic<73>) wide_out = _ASSIGN(cat{
        logic<5>(0x15), logic<0>(0), data_in(), logic<0>(0), data_in(), logic<4>(9)});
    _PORT(logic<32>) effects_out = _ASSIGN(effects());
    _PORT(logic<16>) references_out = _ASSIGN(references());

    unsigned next(unsigned& counter) { return ++counter; }
    unsigned bump(logic<8>& value) { value = uint32_t(value) + 1; return uint32_t(value); }
    logic<16> references() {
        logic<8> source = 1;
        return cat{source, logic<0>(bump(source)), source};
    }
    logic<32> effects() {
        unsigned counter = 0;
        logic<16> packed = cat{logic<8>(next(counter)), logic<0>(next(counter)), logic<8>(next(counter))};
        return (counter << 16) | uint32_t(packed);
    }
};
ZeroConcat cpphdl_top;
