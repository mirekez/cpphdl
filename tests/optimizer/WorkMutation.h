#pragma once

#include "cpphdl.h"

template <typename T>
static inline void update_during_work(T& value)
{
    value = 7;
}

class WorkMutationLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    cpphdl::logic<8> observed = 0;

    void _work(bool)
    {
        observed = input();
    }
};

class WorkMutationRoot : public cpphdl::Module
{
public:
    WorkMutationLeaf leaf;
    cpphdl::logic<8> value = 0;

    cpphdl::logic<8>& value_comb_func()
    {
        return value_comb = value;
    }

    void _assign()
    {
        leaf.input = _ASSIGN_COMB(value_comb_func());
    }

    void _work(bool reset)
    {
        update_during_work(value);
        leaf._work(reset);
    }

private:
    cpphdl::logic<8> value_comb = 0;
};
