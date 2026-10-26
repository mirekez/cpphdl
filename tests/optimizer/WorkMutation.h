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
    cpphdl::logic<8> before = 0;
    cpphdl::logic<8> after = 0;

    cpphdl::logic<8>& procedural_comb_func()
    {
        procedural_comb = 0;
        procedural_comb = value;
        return procedural_comb;
    }

    cpphdl::logic<8>& forwarded_comb_func()
    {
        forwarded_comb = 0;
        forwarded_comb = procedural_comb_func();
        return forwarded_comb;
    }

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
        value = 1;
        before = forwarded_comb_func();
        update_during_work(value);
        after = forwarded_comb_func();
        leaf._work(reset);
    }

private:
    cpphdl::logic<8> value_comb = 0;
    cpphdl::logic<8> procedural_comb = 0;
    cpphdl::logic<8> forwarded_comb = 0;
};
