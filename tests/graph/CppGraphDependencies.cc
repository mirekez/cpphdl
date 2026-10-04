#include "cpphdl.h"

class GraphDependencies : public cpphdl::Module {
public:
    using Word = cpphdl::logic<32>;
    _PORT(Word) data_in;
    _PORT(cpphdl::logic<1>) select_in;
    _PORT(Word) before_out = _ASSIGN_REG(before);
    _PORT(Word) middle_out = _ASSIGN_REG(middle);
    _PORT(Word) refreshed_out = _ASSIGN_REG(refreshed);
    _PORT(Word) after_out = _ASSIGN_REG(after);
    _PORT(Word) result_out = _ASSIGN(parent_func());
    Word state, leaf, left, right, parent;
    cpphdl::reg<Word> before, middle, refreshed, after;
    Word& leaf_func() { leaf = uint64_t(state) + uint64_t(data_in()); return leaf; }
    Word& left_func() { left = uint64_t(leaf_func()) ^ 0x5a5a5a5a; return left; }
    Word& right_func() { right = uint64_t(leaf_func()) + 17; return right; }
    Word& parent_func() { parent = uint64_t(left_func()) + uint64_t(right_func()); return parent; }
    void _work(bool reset) {
        state = data_in();
        before._next = parent_func();
        state = uint64_t(state) + 1;
        middle._next = leaf_func();
        refreshed._next = parent_func();
        if (select_in()) state = uint64_t(state) + 3;
        else state = uint64_t(state) ^ 7;
        if (reset) state = 0;
        after._next = parent_func();
    }
    void _strobe() { before.strobe(); middle.strobe(); refreshed.strobe(); after.strobe(); }
};
GraphDependencies cpphdl_top;

#ifdef CPP_GRAPH_DEPENDENCIES_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    GraphDependencies::Word data;
    cpphdl::logic<1> select;
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.select_in = _ASSIGN(select);
    uint32_t random = 23;
    for (unsigned sample = 0; sample < 4000; ++sample) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        data = random; select = random & 1;
        model.data[0] = random; model.select[0] = random & 1;
        bool reset = sample % 79 == 0;
        model.work_reset[0] = reset;
        ++_system_clock;
        cpphdl_top._work(reset); cpphdl_top._strobe(); ++_system_clock;
        model.step();
        auto expected = [](uint32_t value) { return (value ^ 0x5a5a5a5a) + (value + 17); };
        uint32_t state = random + 1;
        state = (random & 1) ? state + 3 : state ^ 7;
        if (reset) state = 0;
        if (model.before[0] != expected(random + random) ||
            model.middle[0] != uint32_t(random + random + 1) ||
            model.refreshed[0] != expected(random + random + 1) ||
            model.after[0] != expected(state + random) ||
            model.result[0] != expected(state + random) ||
            model.before[0] != uint64_t(cpphdl_top.before_out()) ||
            model.middle[0] != uint64_t(cpphdl_top.middle_out()) ||
            model.refreshed[0] != uint64_t(cpphdl_top.refreshed_out()) ||
            model.after[0] != uint64_t(cpphdl_top.after_out()) ||
            model.result[0] != uint64_t(cpphdl_top.result_out())) {
            std::printf("sample %u input %u state %u: before %u/%u/%lu middle %u/%u/%lu after %u/%u/%lu result %u/%lu\n",
                        sample, random, state, model.before[0], expected(random + random), uint64_t(cpphdl_top.before_out()),
                        model.middle[0], uint32_t(random + random + 1), uint64_t(cpphdl_top.middle_out()),
                        model.after[0], expected(state + random), uint64_t(cpphdl_top.after_out()),
                        model.result[0], uint64_t(cpphdl_top.result_out()));
            std::printf("refreshed %u/%u/%lu\n", model.refreshed[0], expected(random + random + 1),
                        uint64_t(cpphdl_top.refreshed_out()));
            return 1;
        }
    }
    std::puts("ordinary C++ graph: 4000 shared dependency snapshots and blocking invalidations match");
}
#endif
