#include "cpphdl.h"
#ifndef GRAPH_GETTER_DEPTH
#define GRAPH_GETTER_DEPTH 8
#endif

template<unsigned Depth> class GuardedMemoryChain : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<4>) address_in;
    _PORT(cpphdl::logic<64>) data_in;
    _PORT(cpphdl::logic<1>) read_in, write_in;
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN(result_func());
    GuardedMemoryChain<Depth - 1> child;
    cpphdl::logic<64> result_comb;
    cpphdl::logic<64>& result_func() {
        result_comb = uint64_t(child.result_out()) + uint64_t(child.result_out());
        return result_comb;
    }
    void _assign() {
        child.address_in = _ASSIGN(address_in()); child.data_in = _ASSIGN(data_in());
        child.read_in = _ASSIGN(read_in()); child.write_in = _ASSIGN(write_in());
        child._assign();
    }
    void _work(bool reset) { child._work(reset); }
    void _strobe() { child._strobe(); }
};
template<> class GuardedMemoryChain<0> : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<4>) address_in;
    _PORT(cpphdl::logic<64>) data_in;
    _PORT(cpphdl::logic<1>) read_in, write_in;
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN(result_func());
    cpphdl::memory<cpphdl::logic<64>, 1, 12> rows;
    cpphdl::logic<64> result_comb;
    cpphdl::logic<64>& result_func() {
        result_comb = read_in() ? cpphdl::logic<64>(rows[uint64_t(address_in())]) : cpphdl::logic<64>(0);
        return result_comb;
    }
    void _assign() {}
    void _work(bool) { if (write_in()) rows[uint64_t(address_in())] = data_in(); }
    void _strobe() { rows.apply(); }
};
class GuardedMemory : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<4>) address_in;
    _PORT(cpphdl::logic<64>) data_in;
    _PORT(cpphdl::logic<1>) read_in, write_in, enable_a_in, enable_b_in, work_enable_in;
    _PORT(cpphdl::logic<64>) first_out = _ASSIGN(enable_a_in() ? child.result_out() : cpphdl::logic<64>(0));
    _PORT(cpphdl::logic<64>) second_out = _ASSIGN(enable_b_in() ? child.result_out() : cpphdl::logic<64>(0));
    _PORT(cpphdl::logic<64>) work_value_out = _ASSIGN_REG(held);
    cpphdl::reg<cpphdl::logic<64>> held;
    GuardedMemoryChain<GRAPH_GETTER_DEPTH> child;
    void _assign() {
        child.address_in = _ASSIGN(address_in()); child.data_in = _ASSIGN(data_in());
        child.read_in = _ASSIGN(read_in()); child.write_in = _ASSIGN(write_in());
        child._assign();
    }
    void _work(bool reset) {
        held._next = held;
        if (work_enable_in()) held._next = child.result_out();
        child._work(reset);
    }
    void _strobe() { held.strobe(); child._strobe(); }
};
GuardedMemory cpphdl_top;

#ifdef CPP_GRAPH_GUARDED_MEMORY_RUN
#include "model.h"
#include <cassert>
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    cpphdl::logic<4> address;
    cpphdl::logic<64> data;
    cpphdl::logic<1> read = 0, write = 0, a = 0, b = 0, work = 0;
    cpphdl_top.address_in = _ASSIGN(address); cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.read_in = _ASSIGN(read); cpphdl_top.write_in = _ASSIGN(write);
    cpphdl_top.enable_a_in = _ASSIGN(a); cpphdl_top.enable_b_in = _ASSIGN(b);
    cpphdl_top.work_enable_in = _ASSIGN(work);
    cpphdl_top._assign();
    for (unsigned row = 0; row < 12; ++row) {
        address = row; data = row + 1; write = 1;
        model.address[0] = row; model.data[0] = row + 1; model.write[0] = 1;
        cpphdl_top._work(false); cpphdl_top._strobe(); ++_system_clock;
        model.step();
    }
    write = 0; model.write[0] = 0;
    for (unsigned sample = 0; sample < 96; ++sample) {
        address = sample % 12; read = (sample / 12) & 1;
        a = (sample / 24) & 1; b = (sample / 48) & 1;
        model.address[0] = uint64_t(address); model.read[0] = uint64_t(read);
        model.enable_a[0] = uint64_t(a); model.enable_b[0] = uint64_t(b);
        model.eval(false); ++_system_clock;
        uint64_t expected = uint64_t(read) ? (uint64_t(address) + 1) << GRAPH_GETTER_DEPTH : 0;
        assert(uint64_t(cpphdl_top.first_out()) == (uint64_t(a) ? expected : 0));
        assert(uint64_t(cpphdl_top.second_out()) == (uint64_t(b) ? expected : 0));
        assert(model.first[0] == (uint64_t(a) ? expected : 0));
        assert(model.second[0] == (uint64_t(b) ? expected : 0));
        work = 1; model.work_enable[0] = 1;
        cpphdl_top._work(false); cpphdl_top._strobe(); ++_system_clock;
        model.step(); model.eval(false);
        assert(uint64_t(cpphdl_top.work_value_out()) == expected);
        assert(model.work_value[0] == expected);
    }
    model.address[0] = 12;
    model.read[0] = 0; model.enable_a[0] = 1; model.enable_b[0] = 1;
    model.eval(false); // Internal guard suppresses both reads.
    model.read[0] = 1; model.enable_a[0] = 0; model.enable_b[0] = 0;
    model.eval(false); // Both caller guards suppress the invalid address.
    for (unsigned enabled = 1; enabled <= 3; ++enabled) {
        model.enable_a[0] = enabled & 1; model.enable_b[0] = enabled >> 1;
        bool rejected = false;
        try { model.eval(false); } catch (const std::out_of_range&) { rejected = true; }
        assert(rejected);
    }
    model.enable_a[0] = 0; model.enable_b[0] = 0;
    model.eval(false);
    bool rejected = false;
    try { model.step(); } catch (const std::out_of_range&) { rejected = true; }
    assert(rejected); // Reused output getter must check its work-phase caller too.
    std::puts("ordinary C++ graph: shared non-power-of-two RAM getters and caller bounds guards PASS");
}
#endif
