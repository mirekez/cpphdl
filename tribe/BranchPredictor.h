#pragma once

#include "cpphdl.h"
#include "State.h"

using namespace cpphdl;

template<size_t ENTRIES = 16, size_t COUNTER_BITS = 2>
class BranchPredictor : public Module
{
    static_assert(ENTRIES > 0, "BranchPredictor needs at least one entry");
    static_assert((ENTRIES & (ENTRIES - 1)) == 0, "BranchPredictor entries must be a power of two");
    static_assert(COUNTER_BITS > 0 && COUNTER_BITS <= 8, "BranchPredictor counter width must be in 1..8 bits");

    static constexpr size_t INDEX_BITS = ENTRIES <= 1 ? 1 : clog2(ENTRIES);
    static constexpr uint32_t COUNTER_MAX = (1u << COUNTER_BITS) - 1;
    static constexpr uint32_t COUNTER_INIT = COUNTER_MAX >> 1;
public:
    __PORT(bool)      lookup_valid_in;
    __PORT(uint32_t)  lookup_pc_in;
    __PORT(uint32_t)  lookup_target_in;
    __PORT(uint32_t)  lookup_fallthrough_in;
    __PORT(u<4>)      lookup_br_op_in;

    __PORT(bool)      predict_taken_out = __VAR(predict_taken_comb_func());
    __PORT(uint32_t)  predict_next_out = __VAR(predict_next_comb_func());

    __PORT(bool)      update_valid_in;
    __PORT(uint32_t)  update_pc_in;
    __PORT(bool)      update_taken_in;
    __PORT(uint32_t)  update_target_in;

private:
    reg<array<u<COUNTER_BITS>, ENTRIES>> counter_reg;
    reg<array<u32, ENTRIES>> target_reg;
    reg<array<u32, ENTRIES>> tag_reg;
    reg<array<u1, ENTRIES>> valid_reg;

    __LAZY_COMB(lookup_index_comb, u<INDEX_BITS>)
        return lookup_index_comb = (u<INDEX_BITS>)(lookup_pc_in() >> 1);
    }

    __LAZY_COMB(lookup_hit_comb, bool)
        uint32_t index;
        index = (uint32_t)lookup_index_comb_func();
        return lookup_hit_comb = valid_reg[index] && tag_reg[index] == lookup_pc_in();
    }

    __LAZY_COMB(lookup_unconditional_comb, bool)
        return lookup_unconditional_comb =
            lookup_br_op_in() == Br::JAL || lookup_br_op_in() == Br::JALR || lookup_br_op_in() == Br::JR;
    }

    __LAZY_COMB(predict_taken_comb, bool)
        uint32_t index;
        index = (uint32_t)lookup_index_comb_func();
        predict_taken_comb = false;
        if (lookup_valid_in()) {
            if (lookup_unconditional_comb_func()) {
                predict_taken_comb = true;
            }
            else if (lookup_hit_comb_func() && counter_reg[index] >= ((COUNTER_MAX + 1) >> 1)) {
                predict_taken_comb = true;
            }
        }
        return predict_taken_comb;
    }

    __LAZY_COMB(predict_next_comb, uint32_t)
        uint32_t index;
        index = (uint32_t)lookup_index_comb_func();
        predict_next_comb = lookup_fallthrough_in();
        if (predict_taken_comb_func()) {
            predict_next_comb = lookup_hit_comb_func() ? (uint32_t)target_reg[index] : lookup_target_in();
        }
        return predict_next_comb;
    }

public:
    void _work(bool reset)
    {
        uint32_t index;
        uint32_t counter;
        size_t i;

        if (update_valid_in()) {
            index = (update_pc_in() >> 1) & (ENTRIES - 1);
            counter = (uint32_t)counter_reg[index];
            if (update_taken_in()) {
                if (counter != COUNTER_MAX) {
                    counter_reg._next[index] = counter + 1;
                }
            }
            else {
                if (counter != 0) {
                    counter_reg._next[index] = counter - 1;
                }
            }
            target_reg._next[index] = update_target_in();
            tag_reg._next[index] = update_pc_in();
            valid_reg._next[index] = true;
        }

        if (reset) {
            for (i = 0; i < ENTRIES; ++i) {
                counter_reg._next[i] = COUNTER_INIT;
                target_reg._next[i] = 0;
                tag_reg._next[i] = 0;
                valid_reg._next[i] = false;
            }
        }
    }

    void _strobe()
    {
        counter_reg.strobe();
        target_reg.strobe();
        tag_reg.strobe();
        valid_reg.strobe();
    }

    void _assign()
    {
    }
};
