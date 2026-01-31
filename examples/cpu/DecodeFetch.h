#pragma once

using namespace cpphdl;

#include "Instr.h"

template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class DecodeFetch: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

    struct State
    {
        uint32_t pc;
        uint32_t rs1_val;
        uint32_t rs2_val;

        uint32_t imm;

        byte valid:1;
        byte alu_op:4;
        byte mem_op:2;
        byte wb_op:3;
        byte br_op:4;
        byte funct3:3;
        byte rd:5;
        byte rs1:5;
        byte rs2:5;
    };//__PACKED;

    STATE state_comb;
    byte rs1_out_comb;
    byte rs2_out_comb;
    bool stall_comb;

public:
    __PORT(uint32_t)    pc_in;
    __PORT(bool)        instr_valid_in;
    __PORT(uint32_t)    instr_in;
    __PORT(uint32_t)    regs_data0_in;
    __PORT(uint32_t)    regs_data1_in;
    __PORT(byte)        rs1_out        = __VAL( rs1_out_comb );
    __PORT(byte)        rs2_out        = __VAL( rs2_out_comb );
    __PORT(uint32_t)    alu_result_in;  // forwarding from Ex
    __PORT(uint32_t)    mem_data_in;    // forwarding from Mem
    __PORT(bool)        stall_out      = __VAL( stall_comb_func() );

    void _connect()
    {
//        std::print("DecodeFetch: {} of {}\n", ID, LENGTH);
    }

    STATE& state_comb_func()
    {
        bool tmp1;
        uint32_t tmp2;
        Instr instr = {instr_in()};
        if ((instr.raw&3) == 3) {
            instr.decode(state_comb);
            if (instr.r.opcode == 0b0010111) {  // auipc
                state_comb.rs1_val = pc_in();
            }
        }
        else {
            instr.decode16(state_comb);
        }
        tmp1 = instr_valid_in();
        tmp2 = pc_in();
        state_comb.valid = tmp1;
        state_comb.pc = tmp2;

        rs1_out_comb = state_comb.rs1;  // make them separate
        rs2_out_comb = state_comb.rs2;
        return state_comb;
    }

    bool stall_comb_func()
    {
        // hazard
        auto state_comb_tmp = state_comb_func();
        stall_comb = false;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex
            if (state_reg[0].rd == state_comb_tmp.rs1) {
                stall_comb = true;
            }
            if (state_reg[0].rd == state_comb_tmp.rs2) {
                stall_comb = true;
            }
        }
        if ((state_reg[0].valid && state_reg[0].br_op != Br::BNONE)
//            || (state_comb_tmp.valid && state_comb_tmp.br_op != Br::BNONE && ((state_reg[1].valid && (state_reg[1].wb_op == Br::PC2 || (state_reg[1].wb_op == Br::PC4))
//                                                 || (state_reg[2].valid && state_reg[2].wb_op && state_reg[2].br_op != Br::NONE)))
) {
            stall_comb = true;
        }
        return stall_comb;
    }

    void do_decode_fetch()
    {
        auto state_comb_tmp = state_comb_func();

        // fetch
        if (state_comb_tmp.rs1) {
            state_comb_tmp.rs1_val = regs_data0_in();
        }
        if (state_comb_tmp.rs2) {
            state_comb_tmp.rs2_val = regs_data1_in();
        }

        // forwarding
        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem/Wb alu
            if (state_reg[1].rd == state_comb_tmp.rs1) {
                state_comb_tmp.rs1_val = state_in()[ID+1].alu_result;
            }
            if (state_reg[1].rd == state_comb_tmp.rs2) {
                state_comb_tmp.rs2_val = state_in()[ID+1].alu_result;
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex/Mem alu
            if (state_reg[0].rd == state_comb_tmp.rs1) {
                state_comb_tmp.rs1_val = alu_result_in();
            }
            if (state_reg[0].rd == state_comb_tmp.rs2) {
                state_comb_tmp.rs2_val = alu_result_in();
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && state_reg[1].rd != 0) {  // Mem/Wb
            if (state_reg[1].rd == state_comb_tmp.rs1) {
                state_comb_tmp.rs1_val = mem_data_in();
            }
            if (state_reg[1].rd == state_comb_tmp.rs2) {
                state_comb_tmp.rs2_val = mem_data_in();
            }
        }

        state_reg.next[0] = state_comb_tmp;
        state_reg.next[0].valid = instr_valid_in() && !stall_comb_func();
    }

    void _work(bool reset)
    {
        if (reset) {
            state_reg.next[0].valid = 0;
            state_reg.next[1].valid = 0;
            state_reg.next[2].valid = 0;
        }
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_work(reset);  // first because it copies all registers from previous stage
        do_decode_fetch();
//        std::print("!!! {} => {} ", (byte)state_reg.next[0].alu_op, (byte)state_reg[0].alu_op);
    }

    void _strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_strobe();
    }
};
