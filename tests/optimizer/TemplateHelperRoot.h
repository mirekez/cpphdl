#pragma once
#include "cpphdl.h"

class TemplateHelperRoot : public cpphdl::Module {
public:
    cpphdl::logic<16> input;
    cpphdl::logic<16> observed;
    template<typename Target, unsigned Bias>
    static Target convert(const cpphdl::logic<16>& value) {
        return Target(uint64_t(value) + Bias);
    }
    cpphdl::logic<16> value_comb;
    cpphdl::logic<16>& value_comb_func() {
        using Result = decltype(convert<cpphdl::logic<16>, 3>(input));
        value_comb = convert<Result, 3>(input);
        return value_comb;
    }
    void _assign() {}
    void _work(bool reset) { observed = value_comb_func(); }
    void _strobe() {}
};
