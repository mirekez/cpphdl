#pragma once
#include "cpphdl.h"

struct GraphProxyIf : cpphdl::Interface {
    _PORT(uint8_t) data_in;
    _PORT(bool) ready_out;
};
class GraphProxyLeaf : public cpphdl::Module {
public:
    GraphProxyIf bus_in;
    _PORT(uint8_t) result_out;
    void _assign() {
        bus_in.ready_out = _ASSIGN((bus_in.data_in() & 1) != 0);
        result_out = _ASSIGN(uint8_t(bus_in.data_in() * 3));
    }
};
class GraphProxyMiddle : public cpphdl::Module {
public:
    GraphProxyLeaf leaf;
    GraphProxyIf bus_in;
    _PORT(uint8_t) result_out;
    void _assign() {
        assignIf(leaf, *this, leaf.bus_in, bus_in);
        result_out = _ASSIGN(leaf.result_out());
    }
};
class GraphInterfaceProxy : public cpphdl::Module {
public:
    GraphProxyMiddle middle;
    GraphProxyIf bus_in;
    _PORT(uint8_t) result_out;
    void _assign() {
        assignIf(*this, middle, bus_in, middle.bus_in);
        result_out = _ASSIGN(middle.result_out());
    }
};
