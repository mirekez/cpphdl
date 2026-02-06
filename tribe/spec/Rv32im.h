#pragma once

#include "Rv32ic.h"

struct Rv32im: public Rv32ic
{
    void decode(State& state_out)
    {
        state_out = {};

        Rv32ic::decode(state_out);

        if (r.opcode == 0b0110011 && r.funct7 == 0b0000001) {
            state_out.rd = r.rd;
            state_out.wb_op = Wb::ALU;
            if (r.funct3 == 0b000) {
                state_out.alu_op = Alu::MUL;
            }
            if (r.funct3 == 0b101) {
                state_out.alu_op = Alu::DIV;
            }
            if (r.funct3 == 0b011) {
                state_out.alu_op = Alu::MULH;
            }
            state_out.funct3 = r.funct3;
            state_out.rs1 = r.rs1;
            state_out.rs2 = r.rs2;
        }
    }

    std::string mnemonic()
    {
        if (r.opcode == 0b0110011 && r.funct7 == 0b0000001) {
            if (r.funct3 == 0b000) {
                return "mul   ";
            }
            if (r.funct3 == 0b101) {
                return "divu  ";
            }
            if (r.funct3 == 0b011) {
                return "mulhu ";
            }
        }
        return Rv32ic::mnemonic();
    }
};
