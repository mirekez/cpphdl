#pragma once

#include "cpphdl.h"

namespace NativeBitStoreTypes {
using size_t = cpphdl::logic<3>;
}

class NativeBitStoreRoot : public cpphdl::Module
{
public:
    cpphdl::logic<16> input = 0;
    cpphdl::logic<64> result_comb = 0;
    cpphdl::logic<129> wide_comb = 0;
    cpphdl::array<2, cpphdl::logic<4>> array_comb{};
    cpphdl::logic<64> observed = 0;
    cpphdl::logic<129> wide_observed = 0;
    cpphdl::array<2, cpphdl::logic<4>> array_observed{};

    cpphdl::logic<64>& result_comb_func()
    {
        using namespace NativeBitStoreTypes;
        const char* text = "; n0.result_comb[1] = 1;";
        (void)text;
        result_comb[63] = input;
        for (unsigned bit = 0; bit < 8; ++bit) {
            result_comb[bit] = cpphdl::logic<1>(uint64_t(input) >> bit);
        }
        result_comb[8] = result_comb[7] | result_comb[6];
        unsigned position = 5;
        result_comb[position++] = ([&]() {
            position = 12;
            result_comb = uint64_t(result_comb) ^ 0x8000000000004000ull;
            return cpphdl::logic<1>(1);
        }());
        result_comb[([&]() {
            input = uint64_t(input) ^ 1;
            return 13;
        }())] = input;
        result_comb[15] = (cpphdl::logic<8>(0)[3] = input);
        result_comb[16] = input, position = 3;
        result_comb[17] = {1};
        for (unsigned count = 0; result_comb[18] = cpphdl::logic<1>(count < 1); ++count) {}
        return result_comb;
    }

    cpphdl::logic<129>& wide_comb_func()
    {
        wide_comb[0] = cpphdl::logic<8>(2);
        wide_comb[63] = input;
        wide_comb[64] = cpphdl::logic<1>(uint64_t(input) >> 1);
        wide_comb[128] = wide_comb[64] | wide_comb[63];
        return wide_comb;
    }

    cpphdl::array<2, cpphdl::logic<4>>& array_comb_func()
    {
        array_comb[0] = cpphdl::logic<4>(7);
        array_comb[1] = cpphdl::logic<4>(uint64_t(input));
        return array_comb;
    }

    void _work(bool)
    {
        observed = result_comb_func();
        wide_observed = wide_comb_func();
        array_observed = array_comb_func();
    }
    void _strobe() {}
    void _assign() {}
};
