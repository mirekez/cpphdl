#pragma once

#include "cpphdl.h"

class ConstexprBindingChild : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) input;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(input());
};

class ConstexprBindingBus : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<8>) value;
};

template<bool Enabled>
class ConstexprBindingParent : public cpphdl::Module
{
public:
    ConstexprBindingChild child;
    ConstexprBindingChild discarded_child;
    _PORT(ConstexprBindingBus) source;
    _PORT(cpphdl::logic<8>) output = _ASSIGN_COMB(output_comb_func());

    cpphdl::logic<8>& invalid_comb_func()
    {
        invalid_comb = cpphdl::sv_bits<8>(cpphdl::logic<4>(0), 7, 0);
        return invalid_comb;
    }

    cpphdl::logic<8>& output_comb_func()
    {
        if constexpr (Enabled) {
            output_comb = child.output();
        } else {
            output_comb = 0;
        }
        return output_comb;
    }

    void _assign()
    {
        if constexpr (Enabled) {
            child.input = _ASSIGN(source().value());
        }
        if constexpr (Enabled) {
            discarded_child.input = _ASSIGN_COMB(invalid_comb_func());
        }
    }

private:
    cpphdl::logic<8> invalid_comb;
    cpphdl::logic<8> output_comb;
};

class ConstexprBindingRoot : public cpphdl::Module
{
public:
    ConstexprBindingBus bus;
    ConstexprBindingParent<false> disabled;
    ConstexprBindingParent<true> enabled;
    _PORT(cpphdl::logic<8>) disabled_output;
    _PORT(cpphdl::logic<8>) enabled_output;
    cpphdl::logic<1> reset_seen = 0;

    cpphdl::logic<8>& source_comb_func()
    {
        source_comb = 0x5a;
        return source_comb;
    }

    void _assign()
    {
        bus.value = _ASSIGN_COMB(source_comb_func());
        disabled._assign();
        disabled.source = _ASSIGN(bus);
        enabled.source = _ASSIGN(bus);
        enabled._assign();
        disabled_output = _ASSIGN_COMB(disabled.output());
        enabled_output = _ASSIGN_COMB(enabled.output());
    }

    void _work(bool reset)
    {
        reset_seen = reset;
    }

private:
    cpphdl::logic<8> source_comb;
};
