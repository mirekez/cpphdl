#pragma once

#include "cpphdl.h"

class ModuleArrayCallLeaf : public cpphdl::Module
{
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(input());
};

class ModuleArrayCallPortSink : public cpphdl::Module
{
public:
    static constexpr unsigned constant_index = 1;
    _PORT(cpphdl::array<2, ModuleArrayCallLeaf>) sources;
    _PORT(cpphdl::logic<1>) output =
        _ASSIGN_COMB(sources()[(unsigned)((uint64_t)(constant_index))].output());
};

class ModuleArrayCallRoot : public cpphdl::Module
{
public:
    static constexpr unsigned constant_index = 1;
    cpphdl::array<2, ModuleArrayCallLeaf> children;
    ModuleArrayCallPortSink port_sink;
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) select;
    _PORT(cpphdl::logic<1>) output = _ASSIGN_COMB(output_comb_func());
    _PORT(cpphdl::logic<1>) constant_output =
        _ASSIGN_COMB(children[(unsigned)((uint64_t)(constant_index))].output());
    _PORT(cpphdl::logic<1>) port_constant_output =
        _ASSIGN_COMB(port_sink.output());
    cpphdl::logic<1> work_value = 0;

    cpphdl::logic<1>& output_comb_func()
    {
        output_comb = 0;
        output_comb = children[(uint64_t)select()].output();
        return output_comb;
    }

    void _assign()
    {
        children[0].input = _ASSIGN_COMB(input());
        children[1].input = _ASSIGN_COMB(input());
        port_sink.sources = _ASSIGN(children);
    }

    void _work(bool)
    {
        work_value = output_comb_func();
    }

private:
    cpphdl::logic<1> output_comb = 0;
};
