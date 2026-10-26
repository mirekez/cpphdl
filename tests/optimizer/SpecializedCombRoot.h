#pragma once
#include "cpphdl.h"

template<bool Select, unsigned Width = 8>
class SpecializedCombLeaf : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<Width>) input;
    cpphdl::logic<Width> selected_comb = 0;
    cpphdl::logic<Width> scoped_comb = 0;
    cpphdl::logic<Width> scope_value = cpphdl::logic<Width>(9);
    cpphdl::logic<Width> scope_before = 0;
    cpphdl::logic<Width> scope_inside = 0;
    cpphdl::logic<Width> scope_after = 0;
    cpphdl::logic<Width> scope_strobe = 0;
    cpphdl::logic<Width> input_mix = 0;

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
        temporary_count += static_cast<unsigned>(scope_value);
        {
            cpphdl::logic<Width> scope_value = cpphdl::logic<Width>(3);
            temporary_count += static_cast<unsigned>(scope_value) - 3;
        }
        temporary_count -= static_cast<unsigned>(scope_value);
        if constexpr (Select) {
            struct Guard {
                unsigned& count;
                ~Guard() { ++count; }
            } guard{temporary_count};
        }
        scoped_comb = input() + cpphdl::logic<Width>(temporary_count);
        return scoped_comb;
    }
    void _work(bool) {
        scope_before = scope_value;
        {
            cpphdl::logic<Width> scope_value = cpphdl::logic<Width>(3);
            scope_inside = scope_value + this->scope_value;
        }
        scope_after = scope_value;
        {
            cpphdl::logic<Width> input = cpphdl::logic<Width>(2);
            input_mix = input + this->input();
        }
    }
    void _strobe() {
        scope_strobe = scope_value;
        {
            cpphdl::logic<Width> scope_value = cpphdl::logic<Width>(4);
            scope_strobe = scope_strobe + scope_value;
        }
        scope_strobe = scope_strobe + scope_value;
    }
    void _assign() {}
};

class SpecializedCombRoot : public cpphdl::Module {
public:
    cpphdl::logic<8> input = 0;
    cpphdl::logic<8> observed_true = 0;
    cpphdl::logic<8> observed_false = 0;
    cpphdl::logic<8> observed_scope = 0;
    cpphdl::logic<8> scope_value = cpphdl::logic<8>(11);
    cpphdl::logic<8> scope_sum = 0;
    SpecializedCombLeaf<true> positive;
    SpecializedCombLeaf<false> negative;

    void _assign() {
        positive.input = _ASSIGN(input);
        negative.input = _ASSIGN(input);
    }
    void _work(bool) {
        scope_sum = scope_value;
        for (unsigned scope_value = 0; scope_value < 2; ++scope_value) {
            scope_sum = scope_sum + cpphdl::logic<8>(scope_value);
        }
        scope_sum = scope_sum + scope_value;
        positive._work(false);
        negative._work(false);
        observed_true = positive.selected_comb_func();
        observed_false = negative.selected_comb_func();
        observed_scope = positive.scoped_comb_func();
    }
    void _strobe() {
        positive._strobe();
        negative._strobe();
    }
};
