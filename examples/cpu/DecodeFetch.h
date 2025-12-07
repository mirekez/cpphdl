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

        int32_t imm;

        uint8_t valid:1;
        uint8_t alu_op:4;
        uint8_t mem_op:2;
        uint8_t wb_op:2;
        uint8_t br_op:4;
        uint8_t funct3:3;

        uint8_t rd:5;
        uint8_t rs1:5;
        uint8_t rs2:5;
        uint8_t rsv1:1;
    };//__PACKED;

    array<uint8_t,2> regs_rd_id_comb;

    bool stall_comb;

public:
    uint32_t                  *pc_in       = nullptr;
    bool                      *instr_valid_in = nullptr;
    Instr                     *instr_in    = nullptr;
    uint32_t                  *regs_data0_in  = nullptr;  // this input works from combinationsl path of another module from regs_rd_id_comb
    uint32_t                  *regs_data1_in  = nullptr;  // this input works from combinationsl path of another module from regs_rd_id_comb
    array<uint8_t,2>          *regs_rd_id_out = &regs_rd_id_comb;
    uint32_t                  *alu_result_in = nullptr;  // forwarding from Ex
    uint32_t                  *mem_data_in   = nullptr;  // forwarding from Mem
    bool                      *stall_out     = &stall_comb;  // comb !ready

    void connect()
    {
        std::print("DecodeFetch: {} of {}\n", ID, LENGTH);
    }

    array<uint8_t,2> regs_rd_id_comb_func()
    {
        regs_rd_id_comb = {};
        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
            case 0b0100011:  // STORE  // SB, SH, SW
                regs_rd_id_comb[0] = instr_in->s.rs1;
                regs_rd_id_comb[1] = instr_in->s.rs2;
                break;
            case 0b0010011:  // OP-IMM (immediate ALU)
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
            case 0b0110011:  // OP (register ALU)
                regs_rd_id_comb[0] = instr_in->r.rs1;
                regs_rd_id_comb[1] = instr_in->r.rs2;
                break;
            case 0b1100011:  // BRANCH
                regs_rd_id_comb[0] = instr_in->b.rs1;
                regs_rd_id_comb[1] = instr_in->b.rs2;
                break;
            case 0b1100111:  // JALR
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
        }
        return regs_rd_id_comb;
    }

    bool stall_comb_func()
    {
        // hazard
        stall_comb = 0;
        if (state_reg[0].wb_op == Wb::MEMORY && state_reg[0].rd != 0) {  // Ex
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                stall_comb = 1;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                stall_comb = 1;
            }
        }
        if (state_reg[0].wb_op == Wb::PC) {  // Ex
            stall_comb = 1;
        }
        return stall_comb;
    }

    void do_decode_fetch()
    {
        state_reg.next[0] = {};

        state_reg.next[0].rs1 = regs_rd_id_comb_func()[0];
        state_reg.next[0].rs2 = regs_rd_id_comb_func()[1];

        state_reg.next[0].funct3 = 7;
        switch (instr_in->r.opcode) {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].mem_op = Mem::LOAD;
                state_reg.next[0].alu_op = Alu::ADD;    // address = rs1 + imm
                state_reg.next[0].wb_op = Wb::MEMORY;
                state_reg.next[0].funct3 = instr_in->i.funct3;
                break;

            case 0b0100011:  // STORE  // SB, SH, SW
                state_reg.next[0].imm = instr_in->imm_S();
                state_reg.next[0].mem_op = Mem::STORE;
                state_reg.next[0].alu_op = Alu::ADD;    // base + offset
                state_reg.next[0].funct3 = instr_in->i.funct3;
                break;

            case 0b0010011:  // OP-IMM (immediate ALU)
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].wb_op = Wb::ALU;

                switch (instr_in->i.funct3) {
                    case 0b000: state_reg.next[0].alu_op = Alu::ADD; break;     // ADDI
                    case 0b010: state_reg.next[0].alu_op = Alu::SLT; break;     // SLTI
                    case 0b011: state_reg.next[0].alu_op = Alu::SLTU; break;    // SLTIU
                    case 0b100: state_reg.next[0].alu_op = Alu::XOR; break;
                    case 0b110: state_reg.next[0].alu_op = Alu::OR; break;
                    case 0b111: state_reg.next[0].alu_op = Alu::AND; break;
                    case 0b001: state_reg.next[0].alu_op = Alu::SLL; break;
                    case 0b101:
                        state_reg.next[0].alu_op = (instr_in->i.imm11_0 >> 10) & 1 ? Alu::SRA : Alu::SRL;
                        break;
                }
                state_reg.next[0].funct3 = instr_in->i.funct3;
                break;

            case 0b0110011:  // OP (register ALU)
                state_reg.next[0].rd = instr_in->r.rd;
                state_reg.next[0].wb_op = Wb::ALU;

                switch (instr_in->r.funct3) {
                    case 0b000:
                        state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? Alu::SUB : Alu::ADD;
                        break;
                    case 0b111: state_reg.next[0].alu_op = Alu::AND; break;
                    case 0b110: state_reg.next[0].alu_op = Alu::OR;  break;
                    case 0b100: state_reg.next[0].alu_op = Alu::XOR; break;
                    case 0b001: state_reg.next[0].alu_op = Alu::SLL; break;
                    case 0b101: state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? Alu::SRA : Alu::SRL; break;
                    case 0b010: state_reg.next[0].alu_op = Alu::SLT;  break;
                    case 0b011: state_reg.next[0].alu_op = Alu::SLTU; break;
                }
                state_reg.next[0].funct3 = instr_in->r.funct3;
                break;

            case 0b1100011:  // BRANCH
                state_reg.next[0].imm = instr_in->imm_B();
                state_reg.next[0].br_op = Br::BNONE;

                switch (instr_in->b.funct3) {
                    case 0b000: state_reg.next[0].br_op = Br::BEQ; break;
                    case 0b001: state_reg.next[0].br_op = Br::BNE; break;
                    case 0b100: state_reg.next[0].br_op = Br::BLT; break;
                    case 0b101: state_reg.next[0].br_op = Br::BGE; break;
                    case 0b110: state_reg.next[0].br_op = Br::BLTU; break;
                    case 0b111: state_reg.next[0].br_op = Br::BGEU; break;
                }
                state_reg.next[0].funct3 = instr_in->b.funct3;
                break;

            case 0b1101111:  // JAL
                state_reg.next[0].rd  = instr_in->j.rd;
                state_reg.next[0].imm = instr_in->imm_J();
                state_reg.next[0].br_op = Br::JAL;
                state_reg.next[0].wb_op = Wb::PC;
                break;

            case 0b1100111:  // JALR
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].br_op = Br::JALR;
                state_reg.next[0].wb_op = Wb::PC;
                break;

            case 0b0110111:  // LUI
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = instr_in->imm_U();
                state_reg.next[0].alu_op = Alu::PASS_RS1;   // or NONE, since result = imm
                state_reg.next[0].wb_op = Wb::ALU;
                break;

            case 0b0010111:  // AUIPC
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = instr_in->imm_U();
                state_reg.next[0].alu_op = Alu::ADD;  // PC + imm
                state_reg.next[0].wb_op = Wb::ALU;
                break;
        }

        // fetch

        state_reg.next[0].pc = *pc_in;
        state_reg.next[0].rs1_val = *regs_data0_in;
        state_reg.next[0].rs2_val = *regs_data1_in;

        // forwarding
        if (state_reg[2].valid && state_reg[2].wb_op == Wb::ALU && state_reg[2].rd != 0) {  // Wb alu
            if (state_reg[2].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+2].alu_result;
            }
            if (state_reg[2].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+2].alu_result;
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem alu
            if (state_reg[1].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+1].alu_result;
            }
            if (state_reg[1].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+1].alu_result;
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex alu
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *alu_result_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *alu_result_in;
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEMORY && state_reg[1].rd != 0) {  // Mem
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *mem_data_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *mem_data_in;
            }
        }

        state_reg.next[0].valid = *instr_valid_in && stall_comb_func();
    }

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        if (reset) {
            state_reg.next[0].valid = 0;
            state_reg.next[1].valid = 0;
            state_reg.next[2].valid = 0;
            state_reg.next[3].valid = 0;
        }
        do_decode_fetch();
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::work(clk, reset);
    }

    void strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::strobe();
    }
};
