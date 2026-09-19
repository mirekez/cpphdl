#pragma once
#include "cpphdl.h"

template<unsigned Bias, typename Value>
class GeneratedHelper : public cpphdl::Module {
public:
    Value input;
    Value observed;
    _LAZY_COMB(producer, Value)
        producer = uint64_t(input) + Bias;
        return producer;
    }
    Value __hdlcpp_expr_0(unsigned index, Value& input) {
        input = uint64_t(input) + index;
        return uint64_t(producer_func()) ^ uint64_t(input);
    }
    Value __hdlcpp_expr_1() {
        Value local = 7;
        Value total = 0;
        for (unsigned index = 0; index < 4; ++index) {
            total = uint64_t(total) + uint64_t(__hdlcpp_expr_0(index, local));
        }
        return uint64_t(total) + uint64_t(local);
    }
    Value value_comb;
    Value& value_comb_func() {
        value_comb = __hdlcpp_expr_1();
        return value_comb;
    }
    void _assign() {}
    void _work(bool reset) { observed = value_comb_func(); }
    void _strobe() {}
};

class GeneratedHelperRoot : public cpphdl::Module {
public:
    GeneratedHelper<3, cpphdl::logic<16>> narrow;
    GeneratedHelper<9, cpphdl::logic<32>> wide;
    void _assign() {
        narrow._assign();
        wide._assign();
    }
    void _work(bool reset) {
        narrow._work(reset);
        wide._work(reset);
    }
    void _strobe() {}
};
