#pragma once

using namespace cpphdl;

#include "Config.h"

#include "Rv32im.h"
#include "Rv32ia.h"
#include "Zicsr.h"

class Decode: public Module
{
public:
    _PORT(uint32_t) pc_in;
    _PORT(bool)     instr_valid_in;
    _PORT(uint32_t) instr_in;
    _PORT(uint32_t) regs_data0_in;
    _PORT(uint32_t) regs_data1_in;
    _PORT(u<5>)     rs1_out      =  _BIND_VAR( rs1_out_comb_func() );
    _PORT(u<5>)     rs2_out      =  _BIND_VAR( rs2_out_comb_func() );
    _PORT(State)    state_out    =  _BIND_VAR( state_comb_func() );

private:

    _LAZY_COMB(state_comb, State)

#ifdef ENABLE_RV32IA
#ifdef ENABLE_ZICSR
        Zicsr instr = {{{instr_in()}}};
#else
        Rv32ia instr = {{{instr_in()}}};
#endif
#else
#ifdef ENABLE_ZICSR
        Zicsr instr = {{{instr_in()}}};
#else
        Rv32im instr = {{{instr_in()}}};
#endif
#endif
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

    _LAZY_COMB(rs1_out_comb, u<5>)
        rs1_out_comb = state_comb_func().rs1;
        return rs1_out_comb;
    }

    _LAZY_COMB(rs2_out_comb, u<5>)
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

//    void _assign()
//    {
//    }


};
