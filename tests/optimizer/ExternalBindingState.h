#pragma once
#include "cpphdl.h"

class ExternalBindingLeaf;
namespace firtool_cpphdl_external {
void work(ExternalBindingLeaf&, bool);
}

class ExternalBindingLeaf : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<8>) input;
    cpphdl::logic<8> observed = 0;
    void _assign() {}
    void _work(bool reset) { firtool_cpphdl_external::work(*this, reset); }
    void _strobe() {}
};

class ExternalBindingStateRoot : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<8>) input;
    ExternalBindingLeaf leaf;
    unsigned evaluations = 0;
    _LAZY_COMB(value_comb, cpphdl::logic<8>)
        ++evaluations;
        return value_comb = input() + 1;
    }
    void _assign() {
        leaf.input = _ASSIGN(value_comb_func());
        leaf._assign();
    }
    void _work(bool reset) { leaf._work(reset); }
    void _strobe() { leaf._strobe(); }
};
