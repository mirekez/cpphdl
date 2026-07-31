#pragma once

#include "cpphdl.h"

class ModuleArrayCallLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(input());
};

class ModuleArrayCallRoot : public cpphdl::Module
{
public:
    cpphdl::array<1, ModuleArrayCallLeaf> children;
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(output_comb_func());
    cpphdl::logic<1> work_value = 0;

    cpphdl::logic<1>& output_comb_func()
    {
        output_comb = 0;
        for (unsigned index = 0; index < 1; ++index) {
            output_comb = children[index].output();
        }
        return output_comb;
    }

    void _assign()
    {
        for (unsigned index = 0; index < 1; ++index) {
            children[index].input = _ASSIGN_COMB(input());
        }
    }

    void _work(bool)
    {
        work_value = output_comb_func();
    }

private:
    cpphdl::logic<1> output_comb = 0;
};
