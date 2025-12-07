#pragma once

#include <stdint.h>

union Instr
{
    uint32_t raw;
    struct
    {
        uint32_t opcode : 7;
        uint32_t rd     : 5;
        uint32_t funct3 : 3;
        uint32_t rs1    : 5;
        uint32_t rs2    : 5;
        uint32_t funct7 : 7;
    } r;
    struct
    {
        uint32_t opcode : 7;
        uint32_t rd     : 5;
        uint32_t funct3 : 3;
        uint32_t rs1    : 5;
        uint32_t imm11_0: 12;
    } i;
    struct
    {
        uint32_t opcode  : 7;
        uint32_t imm4_0  : 5;
        uint32_t funct3  : 3;
        uint32_t rs1     : 5;
        uint32_t rs2     : 5;
        uint32_t imm11_5 : 7;
    } s;
    struct
    {
        uint32_t opcode    : 7;
        uint32_t imm11     : 1;
        uint32_t imm4_1    : 4;
        uint32_t funct3    : 3;
        uint32_t rs1       : 5;
        uint32_t rs2       : 5;
        uint32_t imm10_5   : 6;
        uint32_t imm12     : 1;
    } b;
    struct
    {
        uint32_t opcode    : 7;
        uint32_t rd        : 5;
        uint32_t imm31_12  : 20;
    } u;
    struct
    {
        uint32_t opcode     : 7;
        uint32_t rd         : 5;
        uint32_t imm19_12   : 8;
        uint32_t imm11      : 1;
        uint32_t imm10_1    : 10;
        uint32_t imm20      : 1;
    } j;

    int32_t sext(uint32_t val, unsigned bits)
    {
        int32_t m = 1u << (bits - 1);
        return (val ^ m) - m; 
    }

    int32_t imm_I()
    {
        return sext(i.imm11_0, 12);
    }

    int32_t imm_S()
    {
        return sext(s.imm4_0 | (s.imm11_5 << 5), 12);
    }

    int32_t imm_B()
    {
        return sext((b.imm4_1 << 1) | (b.imm11  << 11) | (b.imm10_5 << 5) | (b.imm12  << 12), 13);
    }

    int32_t imm_U()
    {
        return int32_t(u.imm31_12 << 12);
    }

    int32_t imm_J()
    {
        return sext((j.imm10_1 << 1) | (j.imm11   << 11) | (j.imm19_12 << 12) | (j.imm20   << 20), 21);
    }

#ifndef SYNTHESIS
    std::string format()
    {
        auto decode_mnemonic = [&](uint32_t op, uint32_t f3, uint32_t f7) -> std::string {
            switch (op) {
            case 0b0110011: // R-type
                if (f3 == 0 && f7 == 0b0000000) return "add   ";
                if (f3 == 0 && f7 == 0b0100000) return "sub   ";
                if (f3 == 7) return "and   ";
                if (f3 == 6) return "or    ";
                if (f3 == 4) return "xor   ";
                if (f3 == 1) return "sll   ";
                if (f3 == 5 && f7 == 0b0000000) return "srl   ";
                if (f3 == 5 && f7 == 0b0100000) return "sra   ";
                if (f3 == 2) return "slt   ";
                if (f3 == 3) return "sltu  ";
                return "r-type";
            case 0b0010011: // I-type ALU
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
        };

        auto m = decode_mnemonic(r.opcode, r.funct3, r.funct7);
        return std::format("[{:08X}]({}), rd:{:02d},rs1:{:02d},rs2:{:02d},f3:{:#03x},f7:{:#04x},imm:{:04x}/{:04x}/{:04x}/{:04x}/{:04x}",
            raw, m, (uint8_t)r.rd, (uint8_t)r.rs1, (uint8_t)r.rs2, (uint8_t)r.funct3, (uint8_t)r.funct7,
            (uint16_t)imm_I(), (uint16_t)imm_S(), (uint16_t)imm_B(), (uint16_t)imm_U(), (uint16_t)imm_J());
    }
#endif
};

enum Alu
{
    ANONE, ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, PASS_RS1,
};

enum Mem
{
    MNONE, LOAD, STORE
};

enum Wb
{
    WNONE, ALU, MEMORY, PC,
};

enum Br
{
    BNONE, BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR
};
