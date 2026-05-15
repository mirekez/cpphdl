#pragma once

#include "Rv32i.h"

struct Rv32ic : public Rv32i
{
    union rv16 {
        uint16_t raw;
        struct {
            uint8_t opcode  : 2;
            uint8_t rd_p    : 3;
            uint8_t bits6_5 : 2;
            uint8_t rs1_p   : 3;
            uint8_t bits11_10:2;
            uint8_t b12     : 1;
            uint8_t funct3  : 3;
        }__PACKED base;
        struct {
            uint8_t opcode  : 2;
            uint8_t rd_p    : 3;
            uint8_t generic : 2;
            uint8_t rs1     : 5;
            uint8_t b12     : 1;
            uint8_t funct3  : 3;
        }__PACKED avg;
        struct {
            uint8_t opcode  : 2;
            uint8_t rs2     : 5;
            uint8_t rs1     : 5;
            uint8_t b12     : 1;
            uint8_t funct3  : 3;
        }__PACKED big;
    };

    uint32_t bit(int lo) { return (raw>>lo) & 1; }
    uint32_t bits(int hi, int lo) { return (raw>>lo) & ((1u<<(hi - lo + 1)) - 1); }

    void decode(State& state_out)
    {
        int32_t imm_tmp;
        uint32_t opcode, funct3, rd_p, rs1_p, bits6_5, bits11_10, b12, rd_rs1, rs2;
        state_out = {};

        if ((raw&3) == 3) {
            Rv32i::decode(state_out);
            return;
        }

        opcode = bits(1, 0);
        funct3 = bits(15, 13);
        rd_p = bits(4, 2);
        rs1_p = bits(9, 7);
        bits6_5 = bits(6, 5);
        bits11_10 = bits(11, 10);
        b12 = bit(12);
        rd_rs1 = bits(11, 7);
        rs2 = bits(6, 2);

        state_out.funct3 = 0b010;  // LW/SW
        if (opcode == 0b00) {
            if (funct3 == 0b000) {  // ADDI4SPN
                state_out.rd = rd_p+8;
                state_out.rs1 = 2; // sp
                state_out.imm = (bits(10,7) << 6) | (bits(12,11) << 4) | (bit(5) << 3) | (bit(6) << 2);
                state_out.alu_op = Alu::ADD;
                state_out.wb_op  = Wb::ALU;
            }
            else if (funct3 == 0b010) {  // LW
                state_out.rd = rd_p+8;
                state_out.rs1 = rs1_p+8;
                state_out.imm = (bit(5)<<6) | (bits(12,10)<<3) | (bit(6)<<2);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::LOAD;
                state_out.wb_op  = Wb::MEM;
            }
            else if (funct3 == 0b110) {  // SW
                state_out.rs1 = rs1_p+8;
                state_out.rs2 = rd_p+8;
                state_out.imm = (bit(5)<<6) | (bits(12,10)<<3) | (bit(6)<<2);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::STORE;
            }
        }
        else if (opcode == 0b01) {
            if (funct3 == 0b000) {  // ADDI
                state_out.rd = rd_rs1;
                state_out.rs1 = rd_rs1;
                imm_tmp = (bit(12) << 5) | bits(6,2);
                imm_tmp = (imm_tmp << 26) >> 26;
                state_out.imm = imm_tmp;
                state_out.alu_op = Alu::ADD;
                state_out.wb_op  = Wb::ALU;
            }
            else if (funct3 == 0b001) {  // JAL
                state_out.rd = 1;
                state_out.wb_op = Wb::PC2;
                state_out.br_op = Br::JAL;
                state_out.imm = sext((b12<<11)|(bit(8)<<10)|(bits(10,9)<<8)|(bit(6)<<7)|(bit(7)<<6)|(bit(2)<<5)|(bit(11)<<4)|(bits(5,3)<<1), 12);
            }
            else if (funct3 == 0b010) {  // LI
                state_out.rd = rd_rs1;
                imm_tmp = (bit(12) << 5) | bits(6, 2);
                imm_tmp = (imm_tmp << 26) >> 26;
                state_out.imm = imm_tmp;
                state_out.alu_op = Alu::PASS;
                state_out.wb_op = Wb::ALU;
            }
            else if (funct3 == 0b011) {  // ADDI16SP / LUI
                if (rd_rs1 == 2) {
                    state_out.rd = 2;
                    state_out.rs1 = 2; // sp
                    imm_tmp = (bit(12) << 9) | (bit(4) << 8) | (bit(3) << 7) | (bit(5) << 6) | (bit(2) << 5) | (bit(6) << 4);
                    state_out.imm = sext(imm_tmp, 10);
                    state_out.alu_op = Alu::ADD;
                    state_out.wb_op = Wb::ALU;
                } else {
                    state_out.rd = rd_rs1;
                    imm_tmp = (bit(12) << 5) | bits(6,2);
                    imm_tmp = (imm_tmp << 26) >> 14;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu::PASS;
                    state_out.wb_op = Wb::ALU;
                }
            }
            else if (funct3 == 0b100) {
                if (bits11_10 == 0) {  // C.SRLI
                    state_out.rd = rs1_p + 8;
                    state_out.rs1 = rs1_p + 8;
                    state_out.imm = bits(6, 2);
                    state_out.alu_op = Alu::SRL;
                    state_out.wb_op = Wb::ALU;
                }
                else if (bits11_10 == 1) {  // C.SRAI
                    state_out.rd = rs1_p + 8;
                    state_out.rs1 = rs1_p + 8;
                    state_out.imm = bits(6, 2);
                    state_out.alu_op = Alu::SRA;
                    state_out.wb_op = Wb::ALU;
                }
                else if (bits11_10 == 2) {  // C.ANDI
                    state_out.rd = rs1_p + 8;
                    state_out.rs1 = rs1_p + 8;
                    imm_tmp = (bit(12) << 5) | bits(6,2);
                    imm_tmp = (imm_tmp << 26) >> 26;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu::AND;
                    state_out.wb_op = Wb::ALU;
                }
                else if (bits11_10 == 3 && b12 == 0) {  // C.SUB, C.XOR, C.OR, C.AND
                    state_out.rd = rs1_p + 8;
                    state_out.rs1 = rs1_p + 8;
                    state_out.rs2 = rd_p + 8;
                    state_out.alu_op = bits6_5 == 0 ? Alu::SUB : ( bits6_5 == 1 ? Alu::XOR : ( bits6_5 == 2 ? Alu::OR : Alu::AND ) );
                    state_out.wb_op = Wb::ALU;
                }
            }
            else if (funct3 == 0b101) {  // J
                state_out.rd = 0;
                state_out.br_op = Br::JAL;
                state_out.imm = sext((b12<<11)|(bit(8)<<10)|(bits(10,9)<<8)|(bit(6)<<7)|(bit(7)<<6)|(bit(2)<<5)|(bit(11)<<4)|(bits(5,3)<<1), 12);
            }
            else if (funct3 == 0b110) {  // BEQZ
                state_out.rs1 = rs1_p+8;
                state_out.br_op = Br::BEQZ;
                state_out.alu_op = Alu::SLTU;
                state_out.imm = (b12<<8)|(bits(6,5)<<6)|(bit(2)<<5)|(bits(11,10)<<3)|(bits(4,3)<<1);
                if (b12) {
                    state_out.imm |= ~0x1FF;
                }
            }
            else if (funct3 == 0b111) {  // BNEZ
                state_out.rs1 = rs1_p+8;
                state_out.br_op = Br::BNEZ;
                state_out.alu_op = Alu::SLTU;
                state_out.imm = (b12<<8)|(bits(6,5)<<6)|(bit(2)<<5)|(bits(11,10)<<3)|(bits(4,3)<<1);
                if (b12) {
                    state_out.imm |= ~0x1FF;
                }
            }
        }
        else if (opcode == 0b10) {
            if (funct3 == 0b000) {  // SLLI
                state_out.rd = rd_rs1;
                state_out.rs1 = rd_rs1;
                state_out.imm = (b12<<5) | bits(6,2);
                state_out.alu_op = Alu::SLL;
                state_out.wb_op  = Wb::ALU;
            }
            else if (funct3 == 0b010) {  // LWSP
                state_out.rd = rd_rs1;
                state_out.rs1 = 2;  // sp
                state_out.imm = (b12<<5) | (bits(6,4)<<2) | (bits(3,2)<<6);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::LOAD;
                state_out.wb_op  = Wb::MEM;
            }
            else if (funct3 == 0b100) {
                if (rs2 != 0) {  // C.MV / C.ADD
                    state_out.rd = rd_rs1;
                    state_out.rs2 = rs2;
                    if (b12 == 0) {
                        state_out.alu_op = Alu::PASS;
                    }
                    else {
                        state_out.rs1 = rd_rs1;
                        state_out.alu_op = Alu::ADD;
                    }
                    state_out.wb_op  = Wb::ALU;
                }
                else if (rs2 == 0 && b12 == 0) {  // C.JR
                    state_out.rs1 = rd_rs1;
                    state_out.br_op = Br::JR;
                    state_out.wb_op = Wb::PC2;
                }
                else if (rs2 == 0 && b12 == 1) {  // C.JALR
                    state_out.rs1 = rd_rs1;
                    state_out.rd = 1;
                    state_out.br_op = Br::JALR;
                    state_out.wb_op = Wb::PC2;
                }
            }
            else if (funct3 == 0b110) {  // SWSP
                state_out.rs1 = 2;  // sp
                state_out.rs2 = rs2;
                state_out.imm = (bits(8,7)<<6) | (bits(12,9)<<2);
                state_out.mem_op = Mem::STORE;
                state_out.alu_op = Alu::ADD;
            }
        }
    }

    std::string mnemonic()
    {
        uint8_t op, f3, b12, rs2, bits6_5, bits11_10;
        if ((raw&3) == 3) {
            return Rv32i::mnemonic();
        }

        op = bits(1, 0);
        f3 = bits(15, 13);
        b12 = bit(12);
        bits6_5 = bits(6, 5);
        bits11_10 = bits(11, 10);
        rs2 = bits(6, 2);
        switch (op) {
        case 0b00:
            switch (f3) {
                case 0b000: return "addi4s";
                case 0b010: return "lw    ";
                case 0b110: return "sw    ";
                case 0b011: return "ld    ";   // RV64/128
                case 0b111: return "sd    ";   // RV64/128
                default:    return "rsrvd ";
            }
        case 0b01:
            switch (f3) {
                case 0b000: return "addi  ";
                case 0b001: return "jal   ";      // RV32 only
                case 0b010: return "li    ";
                case 0b011: return "addisp"; // or lui
                case 0b100: {
                    if (bits11_10 == 0) { return "srli  "; }
                    if (bits11_10 == 1) { return "srai  "; }
                    if (bits11_10 == 2) { return "andi  "; }
                    if (bits11_10 == 3 && b12 == 0 && bits6_5 == 0) { return "sub   "; }
                    if (bits11_10 == 3 && b12 == 0 && bits6_5 == 1) { return "xor   "; }
                    if (bits11_10 == 3 && b12 == 0 && bits6_5 == 2) { return "or    "; }
                    if (bits11_10 == 3 && b12 == 0 && bits6_5 == 3) { return "and   "; }
                    return "illgl ";
                }
                case 0b101: return "j     ";
                case 0b110: return "beqz  ";
                case 0b111: return "bnez  ";
            }
        case 0b10:
            switch (f3) {
                case 0b000: return "slli  ";
                case 0b001: return "fldsp ";
                case 0b010: return "lwsp  ";
                case 0b100: {
                    if (rs2 != 0 && b12 == 0) { return "mv    "; }
                    if (rs2 != 0 && b12 == 1) { return "add   "; }
                    if (rs2 == 0 && b12 == 0) { return "jr    "; }
                    if (rs2 == 0 && b12 == 1) { return "jalr  "; }
                    return "illgl ";
                }
                case 0b110: return "swsp  ";
                case 0b011: return "ldsp  ";  // RV64/128
                case 0b111: return "sdsp  ";  // RV64/128
                default:    return "rsrvd ";
            }
        }
        return "unknwn";
    }
};
