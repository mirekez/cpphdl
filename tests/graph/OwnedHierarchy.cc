#include "cpphdl.h"
using Word = cpphdl::logic<64>;
class OwnedLeaf : public cpphdl::Module {
public:
    _PORT(Word) data_in;
    _PORT(Word) data_out;
    cpphdl::reg<Word> saved{};
    cpphdl::memory<cpphdl::u8, 8, 4> memory;
    void _assign() { data_out = _ASSIGN(saved); }
    void _work(bool reset) {
        auto address = uint64_t(data_in()) % 4;
        saved._next = reset ? Word(0) : Word(memory[address]);
        memory[address] = data_in();
    }
    void _strobe() { saved.strobe(); memory.apply(); }
};
class OwnedHierarchy : public cpphdl::Module {
public:
    _PORT(Word) data_in;
    _PORT(Word) result_out;
    OwnedLeaf *left, *right;
    OwnedHierarchy();
    ~OwnedHierarchy() { delete left; delete right; }
    void _assign() {
        left->data_in = _ASSIGN(data_in());
        right->data_in = _ASSIGN(Word(uint64_t(data_in()) + 17));
        left->_assign(); right->_assign();
        result_out = _ASSIGN(Word(uint64_t(left->data_out()) ^ uint64_t(right->data_out())));
    }
    void _work(bool reset) { left->_work(reset); right->_work(reset); }
    void _strobe() { left->_strobe(); right->_strobe(); }
};
OwnedHierarchy::OwnedHierarchy() { left = new OwnedLeaf(); right = new OwnedLeaf(); }
extern OwnedHierarchy cpphdl_top;

#ifdef CHECK_OWNED_HIERARCHY
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    OwnedHierarchy top;
    cpphdl_native::Model model;
    Word data = 0;
    top.data_in = _ASSIGN(data);
    top._assign();
    uint64_t expectedLeft[4]{}, expectedRight[4]{};
    for (unsigned a = 0; a < 4; ++a) { top.left->memory[a] = 0; top.right->memory[a] = 0; }
    top.left->memory.apply(); top.right->memory.apply();
    for (unsigned cycle = 0; cycle < 1024; ++cycle) {
        uint64_t d = (uint64_t(cycle) << 33) + cycle * 123;
        bool reset = cycle % 23 == 0;
        auto expected = reset ? 0 : expectedLeft[d % 4] ^ expectedRight[(d + 17) % 4];
        expectedLeft[d % 4] = d; expectedRight[(d + 17) % 4] = d + 17;
        data = d; model.data = {uint32_t(d), uint32_t(d >> 32)};
        model.work_reset[0] = reset;
        ++_system_clock; top._work(reset); top._strobe(); ++_system_clock;
        model.step();
        uint64_t actual = model.result[0] | (uint64_t(model.result[1]) << 32);
        if (actual != expected || uint64_t(top.result_out()) != expected) return 1;
    }
    std::puts("owned hierarchy: 1024 cycles PASS");
}
#endif
