#include "cpphdl.h"

template<unsigned Width>
class ExternChild : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<Width>) data_in;
    _PORT(cpphdl::logic<Width>) result_out = _ASSIGN_REG(state);
    cpphdl::reg<cpphdl::logic<Width>> state;
    void _work(bool reset) {
        cpphdl::u32 word = uint64_t(data_in());
        (void)(state._next = reset ? cpphdl::logic<Width>(0) : cpphdl::logic<Width>(uint64_t(word) ^ uint64_t(word[3])));
        if (!reset) state._next.bits(3, 1) = data_in().bits(2, 0);
    }
    void _strobe() { state.strobe(); }
};

template<unsigned Width>
class ExternRoot : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<Width>) data_in__field_byte;
    _PORT(cpphdl::logic<Width>) result_out__field_byte = _ASSIGN_REG(children[1][1].result_out());
    cpphdl::array<2, cpphdl::array<2, ExternChild<Width>>> children;
    ExternRoot() {}
    void _assign() {
        (void)children;
        for (unsigned index = 0; index < 2; ++index)
            for (unsigned lane = 0; lane < 2; ++lane)
                children[index][lane].data_in = _ASSIGN(data_in__field_byte());
    }
    void _work(bool reset) {
        for (unsigned index = 0; index < 2; ++index)
            for (unsigned lane = 0; lane < 2; ++lane) children[index][lane]._work(reset);
    }
    void _strobe() {
        for (unsigned index = 0; index < 2; ++index)
            for (unsigned lane = 0; lane < 2; ++lane) children[index][lane]._strobe();
    }
};

extern ExternRoot<8> cpphdl_top;

#ifdef CPP_GRAPH_EXTERN_RUN
#include "model.h"
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    for (unsigned sample = 0; sample < 1024; ++sample) {
        model.data__field_byte[0] = sample & 255;
        model.work_reset[0] = sample % 17 == 0;
        model.step();
        unsigned expected = model.work_reset[0] ? 0 : model.data__field_byte[0] ^ ((model.data__field_byte[0] >> 3) & 1);
        if (!model.work_reset[0]) expected = (expected & ~14u) | ((model.data__field_byte[0] & 7u) << 1);
        if (model.result__field_byte[0] != expected) return 1;
    }
    std::puts("ordinary C++ graph: extern template root, nested module arrays and projected ports, 1024 transactions match");
}
#endif
