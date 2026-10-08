#include <cpphdl.h>
using namespace cpphdl;

#ifndef EMPTY_REPEAT_COUNT
#define EMPTY_REPEAT_COUNT 0
#endif

template<unsigned Count = 0>
class EmptyRepeatCast : public Module {
public:
    _PORT(logic<32>) pc_in;
    _PORT(logic<32>) result_out, narrow_out;
    _PORT(logic<64>) scalar_out, concat_out, effects_out;
    _PORT(logic<1>) truth_out;
    logic<32> result_comb, narrow_comb;
    logic<64> scalar_comb, concat_comb, effects_comb;
    logic<1> truth_comb;

    logic<32>& result_comb_func() {
        result_comb = uint64_t(repeat<Count, 1>(logic<1>(pc_in()[31]))) | uint64_t(pc_in());
        return result_comb;
    }
    logic<64>& scalar_comb_func() {
        scalar_comb = uint64_t(repeat<Count, 1>(logic<1>(pc_in()[31])));
        return scalar_comb;
    }
    logic<32>& narrow_comb_func() {
        narrow_comb = uint32_t(repeat<Count, 1>(logic<1>(pc_in()[31])));
        return narrow_comb;
    }
    logic<1>& truth_comb_func() {
        truth_comb = bool(repeat<Count, 1>(logic<1>(pc_in()[31])));
        return truth_comb;
    }
    uint64_t scalar_concat(uint64_t high, unsigned high_width, uint64_t low, unsigned low_width) {
        uint64_t packed = 0;
        if (high_width) packed = high;
        if (low_width) packed = (packed << low_width) | low;
        return packed;
    }
    logic<64>& concat_comb_func() {
        // Scalar concat helpers skip empty contributions only after their
        // arguments (including the conversion to uint64_t) are evaluated.
        concat_comb = scalar_concat(uint64_t(repeat<Count, 1>(logic<1>(pc_in()[31]))),
                                    Count, uint64_t(pc_in()), 32);
        return concat_comb;
    }
    unsigned bump(unsigned& counter) { return ++counter; }
    logic<64>& effects_comb_func() {
        unsigned counter = unsigned(pc_in()) & 15;
        uint64_t first = uint64_t(repeat<Count, 1>(logic<1>(bump(counter))));
        uint64_t empty = uint64_t(logic<0>(bump(counter)));
        uint64_t second = 0;
        if (pc_in()[4]) second = uint64_t(repeat<Count, 1>(logic<1>(bump(counter))));
        effects_comb = (uint64_t(counter) << 48) | (first << 16) | second | empty;
        return effects_comb;
    }
    void _assign() {
        result_out = _ASSIGN_COMB(result_comb_func());
        scalar_out = _ASSIGN_COMB(scalar_comb_func());
        concat_out = _ASSIGN_COMB(concat_comb_func());
        narrow_out = _ASSIGN_COMB(narrow_comb_func());
        truth_out = _ASSIGN_COMB(truth_comb_func());
        effects_out = _ASSIGN_COMB(effects_comb_func());
    }
};

extern EmptyRepeatCast<EMPTY_REPEAT_COUNT> cpphdl_top;
