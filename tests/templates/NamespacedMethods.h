#pragma once
#include <cpphdl.h>

namespace alternate {
template<class T> struct Helper {
    T adjust(T value) { return value ^ 17u; }
};
}

namespace cpphdl::hls {
struct NamespacePacket { uint32_t value; } __PACKED;

template<class T> struct Helper {
    T adjust(T value) { return value + 5u; }
};

class NamespacedLeaf : public Module, public Helper<uint32_t>, public alternate::Helper<uint32_t>
{
public:
    _PORT(NamespacePacket) data_in;
    _PORT(logic<32>) value_out;
private:
    reg<logic<32>> value_reg;
public:
    void _assign() { value_out = _ASSIGN_REG(value_reg); }
    void _work(bool reset) {
        value_reg._next = cpphdl::hls::Helper<uint32_t>::adjust(data_in().value)
            + alternate::Helper<uint32_t>::adjust(data_in().value);
        if (reset) value_reg._next = 0;
    }
    void _strobe() { value_reg.strobe(); }
};
}

class NamespacedMethods : public cpphdl::Module
{
    cpphdl::hls::NamespacedLeaf child;
public:
    _PORT(cpphdl::logic<32>) value_in;
    _PORT(cpphdl::logic<32>) value_out;
    void _assign() {
        child.data_in = _ASSIGN(cpphdl::hls::NamespacePacket{uint32_t(value_in())});
        child._assign();
        value_out = child.value_out;
    }
    void _work(bool reset) { child._work(reset); }
    void _strobe() { child._strobe(); }
};
