#pragma once

using namespace cpphdl;

#include "Rv32im.h"

class Decode: public Module
{
    STATIC State state_comb;
    __LAZY_COMB(state_comb, State&)

        Rv32im instr = {{{instr_in()}}};
        instr.decode(state_comb);
        if ((instr.raw&3) == 3 && instr.r.opcode == 0b0010111) {  // auipc needs pc()
            state_comb.rs1_val = pc_in();
        }
        state_comb.valid = instr_valid_in();
        state_comb.pc = pc_in();

        // fetch regs
        if (state_comb.rs1) {
            state_comb.rs1_val = regs_data0_in();
        }
        if (state_comb.rs2) {
            state_comb.rs2_val = regs_data1_in();
        }

        return state_comb;
    }

    STATIC u<5> rs1_out_comb;
    __LAZY_COMB(rs1_out_comb, u<5>&)
        rs1_out_comb = state_comb_func().rs1;
        return rs1_out_comb;
    }

    STATIC u<5> rs2_out_comb;
    __LAZY_COMB(rs2_out_comb, u<5>&)
        rs2_out_comb = state_comb_func().rs2;
        return rs2_out_comb;
    }

public:

    void _work(bool reset)
    {
    }

//    void _strobe()
//    {
//    }

//    void _connect()
//    {
//    }


    __PORT(uint32_t)         pc_in;
    __PORT(bool)    instr_valid_in;
    __PORT(uint32_t)      instr_in;
    __PORT(uint32_t) regs_data0_in;
    __PORT(uint32_t) regs_data1_in;
    __PORT(u<5>)     rs1_out   =       __VAR( rs1_out_comb_func() );
    __PORT(u<5>)     rs2_out   =       __VAR( rs2_out_comb_func() );
    __PORT(State)     state_out =      __VAR( state_comb_func() );
};
