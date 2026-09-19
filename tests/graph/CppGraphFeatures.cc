#include "cpphdl.h"

template<unsigned Width, cpphdl::logic<2> DefaultMask = cpphdl::logic<2>(3)>
class GraphChild : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<Width>) data_in;
    _PORT(cpphdl::logic<2>) select_in;
    _PORT(cpphdl::logic<Width>) value_out = _ASSIGN_REG(state);
    cpphdl::reg<cpphdl::logic<Width>> state;
    void _work(bool reset) {
        state._next = state;
        if (reset) state._next = 0;
        else {
            switch (uint64_t(select_in())) {
            case 0: { state._next = data_in(); break; }
            case 1: { state._next = uint64_t(state) + uint64_t(data_in()); break; }
            case 2: { state._next[0] = data_in(); break; }
            default: { state._next = uint64_t(state) ^ uint64_t(data_in()) ^ uint64_t(DefaultMask); break; }
            }
        }
    }
    void _strobe() { state.strobe(); }
};

class GraphFeatures : public cpphdl::Module {
public:
    _PORT(cpphdl::array<2, cpphdl::logic<8>, true>) data_in;
    _PORT(cpphdl::logic<2>) select_in;
    _PORT(cpphdl::logic<16>) result_out = _ASSIGN(result());
    cpphdl::array<2, GraphChild<8>> children;
    void _assign() {
        for (unsigned index = 0; index < 2; ++index) {
            children[index].data_in = _ASSIGN_INDEXED((index), data_in()[index]);
            children[index].select_in = _ASSIGN(select_in());
        }
    }
    cpphdl::logic<16> result() {
        return cpphdl::cat{children[1].value_out(), children[0].value_out()};
    }
    void _work(bool reset) {
        for (unsigned index = 0; index < 2; ++index) children[index]._work(reset);
    }
    void _strobe() {
        for (unsigned index = 0; index < 2; ++index) children[index]._strobe();
    }
};

GraphFeatures cpphdl_top;

#ifdef CPP_GRAPH_FEATURES_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    cpphdl::array<2, cpphdl::logic<8>, true> data;
    cpphdl::logic<2> select;
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.select_in = _ASSIGN(select);
    cpphdl_top._assign();
    uint32_t random = 23;
    for (unsigned sample = 0; sample < 20000; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        data = cpphdl::logic<16>(random);
        select = random >> 20;
        model.data[0] = random & 65535;
        model.select[0] = uint64_t(select);
        model.work_reset[0] = sample % 79 == 0;
        cpphdl_top._work(model.work_reset[0]);
        cpphdl_top._strobe();
        ++_system_clock;
        model.step();
        if (uint64_t(cpphdl_top.result_out()) != model.result[0]) return 1;
    }
    std::puts("ordinary C++ graph: hierarchy, captured bindings, switch and partial state match 20000 samples");
}
#endif
