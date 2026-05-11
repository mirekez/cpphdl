#pragma once

#include "Config.h"

using namespace cpphdl;

class Writeback: public Module
{
public:
    __PORT(State) state_in;

    __PORT(uint32_t) alu_result_in;
    __PORT(uint32_t) mem_data_in;
    __PORT(uint32_t) mem_data_hi_in;
    __PORT(uint32_t) mem_addr_in;
    __PORT(bool)     mem_split_in;
    __PORT(uint32_t) regs_data_out = __VAR( regs_out_comb_func() );
    __PORT(uint8_t ) regs_wr_id_out = __EXPR( state_in().rd );  // NOTE! reg0 is ZERO, never write it
    __PORT(bool    ) regs_write_out = __VAR( regs_write_comb_func() );

private:

    __LAZY_COMB(mem_word_comb, uint32_t)
        uint32_t shift;
        shift = mem_addr_in() & 3u;
        if (mem_split_in()) {
            mem_word_comb = (mem_data_in() >> (shift * 8u)) |
                (mem_data_hi_in() << ((4u - shift) * 8u));
        }
        else {
            mem_word_comb = mem_data_in();
        }
        return mem_word_comb;
    }

    __LAZY_COMB(regs_out_comb, uint32_t)
        const auto& state_tmp = state_in();
        uint32_t mem_word;

        regs_out_comb = 0;
        mem_word = mem_word_comb_func();
        if (state_tmp.wb_op == Wb::PC2) {
            regs_out_comb = state_tmp.pc + 2;
        } else
        if (state_tmp.wb_op == Wb::PC4) {
            regs_out_comb = state_tmp.pc + 4;
        } else
        if (state_tmp.wb_op == Wb::ALU) {
            regs_out_comb = alu_result_in();
        } else
        if (state_tmp.wb_op == Wb::MEM) {
            switch (state_tmp.funct3) {
                case 0b000: regs_out_comb = int8_t(mem_word);   break;
                case 0b001: regs_out_comb = int16_t(mem_word);  break;
                case 0b010: regs_out_comb = int32_t(mem_word);  break;
                case 0b100: regs_out_comb = uint8_t(mem_word);  break;
                case 0b101: regs_out_comb = uint16_t(mem_word); break;
            }
        }
        return regs_out_comb;
    }

    __LAZY_COMB(regs_write_comb, bool)

        regs_write_comb = 0;
        if (state_in().wb_op != Wb::WNONE) {
            regs_write_comb = state_in().valid;
        }
        return regs_write_comb;
    }

public:

    void _work(bool reset)
    {
    }

    void _strobe()
    {
    }

    void _assign()
    {
    }

};
