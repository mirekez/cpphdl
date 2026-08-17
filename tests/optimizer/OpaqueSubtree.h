#pragma once

#include "cpphdl.h"

class OpaqueSubtreeChild : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(output_comb_func());
    cpphdl::reg<cpphdl::logic<1>> state{};
    unsigned assign_calls = 0;

    cpphdl::logic<1>& output_comb_func()
    {
        output_comb = state;
        return output_comb;
    }

    void _assign() { ++assign_calls; }
    void _work(bool) { state._next = input(); }
    void _strobe() { state.strobe(); }

private:
    cpphdl::logic<1> output_comb = 0;
};

class OpaqueSubtreeRoot : public cpphdl::Module
{
public:
    OpaqueSubtreeChild child;
    _PORT(cpphdl::logic<1>) input;
    cpphdl::logic<1> observed = 0;

    void _assign()
    {
        child.input = _ASSIGN_COMB(input());
        child._assign();
    }

    void _work(bool reset)
    {
        child._work(reset);
        observed = forwarded_comb_func();
    }

    void _strobe() { child._strobe(); }

private:
    cpphdl::logic<1> forwarded_comb = 0;
    cpphdl::logic<1>& forwarded_comb_func()
    {
        forwarded_comb = child.output();
        return forwarded_comb;
    }
};
