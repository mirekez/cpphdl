#include "cpphdl.h"
class FirtoolCompare : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) reset_in;
    _PORT(cpphdl::logic<4>) count_out;
    _PORT(cpphdl::logic<1>) different_out;
    _PORT(cpphdl::logic<129>) left_in;
    _PORT(cpphdl::logic<129>) right_in;
    _PORT(cpphdl::logic<1>) equal_out;
    _PORT(cpphdl::logic<1>) unequal_out;
    _PORT(cpphdl::logic<1>) less_out;
    _PORT(cpphdl::logic<1>) greater_out;
    cpphdl::reg<cpphdl::logic<4>> count{};
    inline static constexpr cpphdl::logic<4> limit = 7;
    void _assign() {
        count_out = _ASSIGN(count);
        different_out = _ASSIGN(cpphdl::logic<1>(count != limit));
        equal_out = _ASSIGN(cpphdl::logic<1>(left_in() == right_in()));
        unequal_out = _ASSIGN(cpphdl::logic<1>(left_in() != right_in()));
        less_out = _ASSIGN(cpphdl::logic<1>(left_in() < right_in()));
        greater_out = _ASSIGN(cpphdl::logic<1>(left_in() > right_in()));
    }
    void _work(bool) {
        if (reset_in()) count._next = 0;
        else count._next = (count == limit) ? cpphdl::logic<4>(0) : cpphdl::logic<4>(count + 1);
    }
    void _strobe() { count.strobe(); }
};
extern FirtoolCompare cpphdl_top;
#ifdef CHECK_FIRTOOL_COMPARE
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    FirtoolCompare top;
    cpphdl_native::Model model;
    cpphdl::logic<1> reset;
    cpphdl::logic<129> left=0, right=0;
    top.left_in = _ASSIGN(left); top.right_in = _ASSIGN(right);
    top.reset_in = _ASSIGN(reset); top._assign();
    unsigned expected = 0;
    for (unsigned cycle = 0; cycle < 1024; ++cycle) {
        reset = cycle % 31 == 0;
        expected = reset ? 0 : (expected + 1) % 8;
        model.reset[0] = uint64_t(reset);
        left=0; right=0;
        left.set(cycle%129,1); right=left;
        bool equal=cycle%3==0;
        if(!equal) right.set((cycle*37)%129,!right.get((cycle*37)%129));
        bool less=false, greater=false;
        for(int bit=128;bit>=0;--bit) if(left.get(bit)!=right.get(bit)) {
            less=right.get(bit); greater=left.get(bit); break;
        }
        model.left.fill(0); model.right.fill(0);
        for(unsigned bit=0;bit<129;++bit) {
            model.left[bit/32] |= uint32_t(left.get(bit)) << (bit%32);
            model.right[bit/32] |= uint32_t(right.get(bit)) << (bit%32);
        }
        ++_system_clock; top._work(false); top._strobe(); ++_system_clock;
        model.step();
        if (uint64_t(top.count_out()) != expected || model.count[0] != expected ||
            uint64_t(top.different_out()) != (expected != 7) || model.different[0] != (expected != 7) ||
            bool(top.equal_out()) != equal || model.equal[0] != equal ||
            bool(top.unequal_out()) == equal || model.unequal[0] == equal ||
            bool(top.less_out()) != less || model.less[0] != less ||
            bool(top.greater_out()) != greater || model.greater[0] != greater) return 1;
    }
    std::puts("firtool rewritten comparisons: 1024 cycles PASS");
}
#endif
