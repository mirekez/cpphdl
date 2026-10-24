#pragma once
#include <cpphdl.h>

using namespace cpphdl;

template<unsigned WIDTH>
class RootInstanceLeaf : public Module
{
public:
    _PORT(logic<WIDTH>) value_in;
    _PORT(logic<WIDTH>) value_out;

private:
    reg<logic<WIDTH>> value_reg;

public:
    void _assign() { value_out = _ASSIGN_REG(value_reg); }
    void _work(bool reset) { value_reg._next = reset ? 0 : uint32_t(value_in()) + 3; }
    void _strobe() { value_reg.strobe(); }
};

template<unsigned WIDTH>
class RootInstanceTop : public Module
{
    RootInstanceLeaf<WIDTH> child;

public:
    _PORT(logic<WIDTH>) value_in;
    _PORT(logic<WIDTH>) value_out;

    void _assign()
    {
        child.value_in = value_in;
        child._assign();
        value_out = child.value_out;
    }
    void _work(bool reset) { child._work(reset); }
    void _strobe() { child._strobe(); }
};

// No explicit instantiation or method calls: the converter must elaborate
// methods starting from standalone objects, including C++ object arrays.
#ifdef ROOT_INSTANCE_ARRAY
RootInstanceTop<8> root_instances[2];
#else
RootInstanceTop<8> root_instance;
#endif
