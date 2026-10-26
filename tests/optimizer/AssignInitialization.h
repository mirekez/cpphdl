#pragma once

#include "cpphdl.h"

class AssignInitializationLeaf : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) input;
    cpphdl::logic<32> initial_value{};
    cpphdl::logic<32> observed{};
    cpphdl::reg<cpphdl::logic<32>> initialized{};
    unsigned assignments{};

    void _assign() {
        initial_value = input();
        initialized = cpphdl::logic<32>(5);
        assignments = assignments + 1;
    }
    void _work(bool) { observed = input() + initial_value; }
    void _strobe() {}
};

#if ASSIGN_REJECT == 1
class AssignInitialization : public cpphdl::Module {
public:
    cpphdl::logic<32> value{};
    _PORT(cpphdl::logic<32>) input;
    void _assign() {
        input = [this]() { return &value; };
    }
    void _work(bool) {}
    void _strobe() {}
};
#elif ASSIGN_REJECT == 2
class AssignInitialization : public cpphdl::Module {
public:
    cpphdl::logic<32> value{};
    void initialize() { value = cpphdl::logic<32>(0); }
    void _assign() {
        initialize();
    }
    void _work(bool) {}
    void _strobe() {}
};
#else
class AssignInitialization : public cpphdl::Module {
public:
    cpphdl::logic<32> value = cpphdl::logic<32>(99);
    cpphdl::logic<32> before_child{};
    cpphdl::logic<32> after_child{};
    unsigned assignments{};
    _PORT(cpphdl::logic<32>) input;
    _PORT(cpphdl::logic<32>) output;
    AssignInitializationLeaf leaf;

    void _assign() {
        this->value = cpphdl::logic<32>(17);
        input = _ASSIGN_REG(value);
        before_child = input();
        leaf.input = _ASSIGN_REG(value);
        leaf._assign();
        value = cpphdl::logic<32>(23);
        after_child = leaf.initial_value;
        assignments = assignments + 1;
        output = _ASSIGN_REG(leaf.observed);
    }
    void _work(bool reset) { leaf._work(reset); }
    void _strobe() { leaf._strobe(); }
};
#endif
