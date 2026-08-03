#pragma once

#include "cpphdl.h"

class LazyCycleChild : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(output_comb_func());
    unsigned evaluations = 0;

    cpphdl::logic<1>& output_comb_func()
    {
        ++evaluations;
        output_comb = input();
        return output_comb;
    }

private:
    cpphdl::logic<1> output_comb = 0;
};

class LazyCycleOrderRoot : public cpphdl::Module
{
public:
    LazyCycleChild child;
    _PORT(cpphdl::logic<1>) output;
    _PORT(cpphdl::logic<1>) inactive_input;
    cpphdl::logic<1> work_value = 0;
    cpphdl::logic<1> work_output = 0;
    cpphdl::logic<1> use_inactive_input = 0;
    unsigned evaluations = 0;

    cpphdl::logic<1>& producer_comb_func()
    {
        ++evaluations;
        if (use_inactive_input) {
            producer_comb = inactive_input();
        } else {
            producer_comb = cpphdl::logic<1>(1) & ~child.output();
        }
        return producer_comb;
    }

    void _assign()
    {
        child.input = _ASSIGN_COMB(producer_comb_func());
        output = _ASSIGN_COMB(child.output());
    }

    void _work(bool)
    {
        work_value = producer_comb_func();
        work_output = child.output();
    }

private:
    cpphdl::logic<1> producer_comb = 0;
};
