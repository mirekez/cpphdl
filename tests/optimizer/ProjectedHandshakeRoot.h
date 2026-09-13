#pragma once
#include "cpphdl.h"

struct ProjectedHandshakeBus {
    cpphdl::logic<1> valid = 0;
    cpphdl::logic<1> ready = 0;
};

class ProjectedHandshakeLeaf : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) valid_in;
    _PORT(cpphdl::logic<1>) ready_in;
    _PORT(ProjectedHandshakeBus) bus_out = _ASSIGN_COMB(bus_comb_func());
    _PORT(cpphdl::logic<1>) bus_out__field_valid = _ASSIGN_COMB(valid_comb_func());
    _PORT(cpphdl::logic<1>) bus_out__field_ready = _ASSIGN_COMB(ready_comb_func());

    _LAZY_COMB(bus_comb, ProjectedHandshakeBus)
        bus_comb = {};
        bus_comb.ready = ready_in();
        bus_comb.valid = valid_in();
        return bus_comb;
    }
    _LAZY_COMB(valid_comb, cpphdl::logic<1>)
        valid_comb = valid_in();
        return valid_comb;
    }
    _LAZY_COMB(ready_comb, cpphdl::logic<1>)
        ready_comb = ready_in();
        return ready_comb;
    }
    void _assign() {}
    void _work(bool) {}
    void _strobe() {}
};

class ProjectedHandshakeRoot : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) input;
    _PORT(cpphdl::logic<1>) valid = _ASSIGN_COMB(leaf.bus_out__field_valid());
    _PORT(cpphdl::logic<1>) ready = _ASSIGN_COMB(leaf.bus_out__field_ready());
    ProjectedHandshakeLeaf leaf;

    void _assign()
    {
        leaf.valid_in = _ASSIGN_COMB(input());
        // This graph is acyclic at field granularity: input -> valid -> ready.
        // Replacing either projection with the whole bus creates a false cycle.
        leaf.ready_in = _ASSIGN_COMB(leaf.bus_out__field_valid());
        leaf._assign();
    }
    void _work(bool reset) { leaf._work(reset); }
    void _strobe() { leaf._strobe(); }
};
