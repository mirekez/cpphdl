#pragma once
#include "cpphdl.h"

template<bool Select, unsigned Width = 8>
class SpecializedCombLeaf : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<Width>) input;
    cpphdl::logic<Width> selected_comb = 0;
    cpphdl::logic<Width> scoped_comb = 0;

    cpphdl::logic<Width>& selected_comb_func() {
        if constexpr (Select) {
            selected_comb = input() + cpphdl::logic<Width>(3);
        }
        if constexpr (!Select) {
            selected_comb = input() + cpphdl::logic<Width>(7);
        }
        return selected_comb;
    }

    cpphdl::logic<Width>& scoped_comb_func() {
        unsigned temporary_count = 0;
        if constexpr (Select) {
            struct Guard {
                unsigned& count;
                ~Guard() { ++count; }
            } guard{temporary_count};
        }
        scoped_comb = input() + cpphdl::logic<Width>(temporary_count);
        return scoped_comb;
    }
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class SpecializedCombRoot : public cpphdl::Module {
public:
    cpphdl::logic<8> input = 0;
    cpphdl::logic<8> observed_true = 0;
    cpphdl::logic<8> observed_false = 0;
    cpphdl::logic<8> observed_scope = 0;
    SpecializedCombLeaf<true> positive;
    SpecializedCombLeaf<false> negative;

    void _assign() {
        positive.input = _ASSIGN(input);
        negative.input = _ASSIGN(input);
    }
    void _work(bool) {
        observed_true = positive.selected_comb_func();
        observed_false = negative.selected_comb_func();
        observed_scope = positive.scoped_comb_func();
    }
    void _strobe() {}
};
