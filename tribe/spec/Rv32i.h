#pragma once

#include "State.h"

struct Rv32i
{
    union {
        uint32_t raw;
        struct
        {
            uint8_t opcode : 7;
            uint8_t rd     : 5;
            uint8_t funct3 : 3;
            uint8_t rs1    : 5;
            uint8_t rs2    : 5;
            uint8_t funct7 : 7;
        }__PACKED r;
        struct
        {
            uint8_t  opcode : 7;
            uint8_t  rd     : 5;
            uint8_t  funct3 : 3;
            uint8_t  rs1    : 5;
            uint16_t imm11_0: 12;
        }__PACKED i;
        struct
        {
            uint8_t opcode  : 7;
            uint8_t imm4_0  : 5;
            uint8_t funct3  : 3;
            uint8_t rs1     : 5;
            uint8_t rs2     : 5;
            uint8_t imm11_5 : 7;
        }__PACKED s;
        struct
        {
            uint8_t opcode    : 7;
            uint8_t imm11     : 1;
            uint8_t imm4_1    : 4;
            uint8_t funct3    : 3;
            uint8_t rs1       : 5;
            uint8_t rs2       : 5;
            uint8_t imm10_5   : 6;
            uint8_t imm12     : 1;
        }__PACKED b;
        struct
        {
            uint8_t  opcode    : 7;
            uint8_t  rd        : 5;
            uint32_t imm31_12  : 20;
        }__PACKED u;
        struct
        {
            uint8_t  opcode     : 7;
            uint8_t  rd         : 5;
            uint8_t  imm19_12   : 8;
            uint8_t  imm11      : 1;
            uint16_t imm10_1    : 10;
            uint8_t  imm20      : 1;
        }__PACKED j;
    };

    int32_t sext(uint32_t val, unsigned bits)
    {
        int32_t m = 1u<<(bits - 1);
        return (val ^ m) - m; 
    }

    int32_t imm_I() { return sext(i.imm11_0, 12); }
    int32_t imm_S() { return sext(s.imm4_0 | (s.imm11_5<<5), 12); }
    int32_t imm_B() { return sext((b.imm4_1<<1) | (b.imm11<<11) | (b.imm10_5<<5) | (b.imm12<<12), 13); }
    int32_t imm_U() { return int32_t(u.imm31_12<<12); }
    int32_t imm_J() { return sext((j.imm10_1<<1) | (j.imm11<<11) | (j.imm19_12<<12) | (j.imm20<<20), 21); }

    void decode(State& state_out)
    {
        state_out = {};

        if (r.opcode == 0b0000011) {  // LOAD  // LB, LH, LW, LBU, LHU
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.mem_op = Mem::LOAD;
            state_out.alu_op = Alu::ADD;    // address = rs1 + imm
            state_out.wb_op = Wb::MEM;
            state_out.funct3 = i.funct3;
            state_out.rs1 = i.rs1;
        } else
        if (r.opcode == 0b0100011) {  // STORE  // SB, SH, SW
            state_out.imm = imm_S();
            state_out.mem_op = Mem::STORE;
            state_out.alu_op = Alu::ADD;    // base + offset
            state_out.funct3 = s.funct3;
            state_out.rs1 = s.rs1;
            state_out.rs2 = s.rs2;
        } else
        if (r.opcode == 0b0010011) {  // OP-IMM (immediate ALU)
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.wb_op = Wb::ALU;
            switch (i.funct3) {
                case 0b000: state_out.alu_op = Alu::ADD; break;     // ADDI
                case 0b010: state_out.alu_op = Alu::SLT; break;     // SLTI
                case 0b011: state_out.alu_op = Alu::SLTU; break;    // SLTIU
                case 0b100: state_out.alu_op = Alu::XOR; break;
                case 0b110: state_out.alu_op = Alu::OR; break;
                case 0b111: state_out.alu_op = Alu::AND; break;
                case 0b001: state_out.alu_op = Alu::SLL; break;
                case 0b101: state_out.alu_op = (i.imm11_0>>10) & 1 ? Alu::SRA : Alu::SRL; break;
            }
            state_out.funct3 = i.funct3;
            state_out.rs1 = i.rs1;
        } else
        if (r.opcode == 0b0110011) {  // OP (register ALU)
            state_out.rd = r.rd;
            state_out.wb_op = Wb::ALU;
            switch (r.funct3) {
                case 0b000: state_out.alu_op = (r.funct7 == 0b0100000) ? Alu::SUB : Alu::ADD; break;
                case 0b111: state_out.alu_op = (r.funct7 == 0b0000001) ? Alu::REM : Alu::AND; break;
                case 0b110: state_out.alu_op = Alu::OR;  break;
                case 0b100: state_out.alu_op = Alu::XOR; break;
                case 0b001: state_out.alu_op = Alu::SLL; break;
                case 0b101: state_out.alu_op = (r.funct7 == 0b0100000) ? Alu::SRA : Alu::SRL; break;
                case 0b010: state_out.alu_op = Alu::SLT;  break;
                case 0b011: state_out.alu_op = Alu::SLTU; break;
            }
            state_out.funct3 = r.funct3;
            state_out.rs1 = r.rs1;
            state_out.rs2 = r.rs2;
        } else
        if (r.opcode == 0b1100011) {  // BRANCH
            state_out.imm = imm_B();
            state_out.br_op = Br::BNONE;
            switch (b.funct3) {
                case 0b000: state_out.br_op = Br::BEQ; state_out.alu_op = Alu::SLTU; break;
                case 0b001: state_out.br_op = Br::BNE; state_out.alu_op = Alu::SLTU; break;
                case 0b100: state_out.br_op = Br::BLT; state_out.alu_op = Alu::SLT; break;
                case 0b101: state_out.br_op = Br::BGE; state_out.alu_op = Alu::SLT; break;
                case 0b110: state_out.br_op = Br::BLTU; state_out.alu_op = Alu::SLTU; break;
                case 0b111: state_out.br_op = Br::BGEU; state_out.alu_op = Alu::SLTU; break;
            }
            state_out.funct3 = b.funct3;
            state_out.rs1 = b.rs1;
            state_out.rs2 = b.rs2;
        } else
        if (r.opcode == 0b1101111) {  // JAL
            state_out.rd  = j.rd;
            state_out.imm = imm_J();
            state_out.br_op = Br::JAL;
            state_out.wb_op = Wb::PC4;
        } else
        if (r.opcode == 0b1100111) {  // JALR
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.br_op = Br::JALR;
            state_out.wb_op = Wb::PC4;
            state_out.rs1 = i.rs1;
        } else
        if (r.opcode == 0b0110111) {  // LUI
            state_out.rd  = u.rd;
            state_out.imm = imm_U();
            state_out.alu_op = Alu::PASS;   // or NONE, since result = imm
            state_out.wb_op = Wb::ALU;
        } else
        if (r.opcode == 0b0010111) {  // AUIPC
            state_out.rd  = u.rd;
            state_out.imm = imm_U();
            state_out.alu_op = Alu::ADD;  // PC + imm
            state_out.wb_op = Wb::ALU;
        }
//      else if (r.opcode == 0b1110011) {  // CSR
//            switch (i.funct3) {
//            case 0b001:  // CSRRW
//            case 0b010:  // CSRRS
//            case 0b011:  // CSRRC
//                regs_rd_id_comb[0] = i.rs1;
//            break;
//        }
    }

    std::string mnemonic()
    {
        uint32_t op, f3, f7;
        op = r.opcode;
        f3 = r.funct3;
        f7 = r.funct7;
        switch (op) {
        case 0b0110011:  // R-type
            if (f3 == 0 && f7 == 0b0000000) return "add   ";
            if (f3 == 0 && f7 == 0b0100000) return "sub   ";
            if (f3 == 0 && f7 == 0b0000001) return "mul   ";
            if (f3 == 7 && f7 == 0b0000001) return "remu  ";
            if (f3 == 7) return "and   ";
            if (f3 == 6) return "or    ";
            if (f3 == 4) return "xor   ";
            if (f3 == 1) return "sll   ";
            if (f3 == 5 && f7 == 0b0000000) return "srl   ";
            if (f3 == 5 && f7 == 0b0100000) return "sra   ";
            if (f3 == 5 && f7 == 0b0000001) return "divu  ";
            if (f3 == 2) return "slt   ";
            if (f3 == 3 && f7 == 0b0000001) return "mulhu ";
            if (f3 == 3) return "sltu  ";
            return "r-type";
        case 0b0010011:  // I-type ALU
            if (f3 == 0) return "addi  ";
            if (f3 == 7) return "andi  ";
            if (f3 == 6) return "ori   ";
            if (f3 == 4) return "xori  ";
            if (f3 == 1) return "slli  ";
            if (f3 == 5 && f7 == 0) return "srli  ";
            if (f3 == 5 && f7 == 0b0100000) return "srai  ";
            if (f3 == 2) return "slti  ";
            if (f3 == 3) return "sltiu ";
            return "aluimm";
        case 0b0000011: return "load  ";
        case 0b0100011: return "store ";
        case 0b1100011: return "branch";
        case 0b1101111: return "jal   ";
        case 0b1100111: return "jalr  ";
        case 0b0110111: return "lui   ";
        case 0b0010111: return "auipc ";
        default:        return "unknwn";
        }
        return "unknwn";
    }
};
