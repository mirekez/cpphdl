#pragma once

#include "cpphdl.h"

class DynamicCombRepeatLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(input());
};

class DynamicCombRepeatRoot : public cpphdl::Module
{
public:
    DynamicCombRepeatLeaf leaf;
    DynamicCombRepeatLeaf lazy_leaf;
    cpphdl::logic<1> work_output = 0;
    cpphdl::logic<3> first_repeat = 0;
    cpphdl::logic<3> second_repeat = 0;
    cpphdl::logic<3> first_lazy = 0;
    cpphdl::logic<3> second_lazy = 0;
    unsigned lazy_evaluations = 0;

    cpphdl::logic<3>& advancing_comb_func()
    {
        advancing_comb[2] = advancing_comb[1];
        advancing_comb[1] = advancing_comb[0];
        advancing_comb[0] = 1;
        return advancing_comb;
    }

    _LAZY_COMB(lazy_comb, cpphdl::logic<3>)
        ++lazy_evaluations;
        return lazy_comb = 5;
    }

    void _assign()
    {
        if constexpr (true) {
            leaf.input = _ASSIGN(advancing_comb_func()[2]);
            lazy_leaf.input = _ASSIGN(lazy_comb_func()[2]);
        }
    }

    void _work(bool)
    {
        work_output = leaf.output();
        first_repeat = advancing_comb_func();
        second_repeat = advancing_comb_func();
        first_lazy = lazy_comb_func();
        second_lazy = lazy_comb_func();
    }

private:
    cpphdl::logic<3> advancing_comb = 0;
};
