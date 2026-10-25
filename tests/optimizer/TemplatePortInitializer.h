#pragma once
#include "cpphdl.h"

template <typename Value>
class InitializedPortLeaf : public cpphdl::Module {
public:
    _PORT(Value) output = _ASSIGN_COMB(result_comb_func());
    Value value = 13;

    _LAZY_COMB(result_comb, Value)
        result_comb = value;
        return result_comb;
    }
    void _assign() {}
    void _work(bool) {}
    void _strobe() {}
};

class TemplatePortInitializer : public cpphdl::Module {
public:
    InitializedPortLeaf<cpphdl::logic<32>> wide;
    InitializedPortLeaf<cpphdl::logic<8>> narrow;
    cpphdl::logic<32> observed;
    cpphdl::logic<8> truncated;

    void _assign() {
        wide._assign();
        narrow._assign();
    }
    void _work(bool) {
        observed = wide.output();
        truncated = narrow.output();
    }
    void _strobe() {}
};
