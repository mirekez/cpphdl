#pragma once

#include "cpphdl.h"

class PureCombLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<8>& output_comb_func()
    {
        output_comb = input() + cpphdl::logic<8>(3);
        return output_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}

private:
    cpphdl::logic<8> output_comb = 0;
};

class PureCombRoot : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(child.output());
    PureCombLeaf child;

    void _work(bool reset) { child._work(reset); }
    void _strobe() { child._strobe(); }
    void _assign()
    {
        child.input = _ASSIGN_COMB(input());
        child._assign();
    }
};
