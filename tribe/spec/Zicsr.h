#pragma once

#include "Rv32im.h"

struct Zicsr: public Rv32im
{
    void decode(State& state_out)
    {
        state_out = {};

        Rv32im::decode(state_out);

        if ((raw & 3) == 3 && r.opcode == 0b1110011 && i.funct3 == 0) {
            if (raw == 0x00000073) {
                state_out.sys_op = Sys::ECALL;
                state_out.br_op = Br::JR;
            }
            else if (raw == 0x30200073) {
                state_out.sys_op = Sys::MRET;
                state_out.br_op = Br::JR;
            }
        }

        if ((raw & 3) == 3 && r.opcode == 0b1110011 && i.funct3 != 0) {
            state_out.rd = i.rd;
            state_out.funct3 = i.funct3;
            state_out.csr_addr = i.imm11_0;
            state_out.csr_imm = i.rs1;

            switch (i.funct3) {
                case 0b001:
                    state_out.csr_op = Csr::CSRRW;
                    state_out.rs1 = i.rs1;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                case 0b010:
                    state_out.csr_op = Csr::CSRRS;
                    state_out.rs1 = i.rs1;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                case 0b011:
                    state_out.csr_op = Csr::CSRRC;
                    state_out.rs1 = i.rs1;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                case 0b101:
                    state_out.csr_op = Csr::CSRRWI;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                case 0b110:
                    state_out.csr_op = Csr::CSRRSI;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                case 0b111:
                    state_out.csr_op = Csr::CSRRCI;
                    state_out.wb_op = i.rd ? Wb::ALU : Wb::WNONE;
                    break;
                default:
                    state_out = {};
                    break;
            }
        }
    }

    std::string mnemonic()
    {
        if ((raw & 3) == 3 && r.opcode == 0b1110011) {
            if (raw == 0x00000073) { return "ecall "; }
            if (raw == 0x30200073) { return "mret  "; }
            if (i.funct3 == 0b001) { return "csrrw "; }
            if (i.funct3 == 0b010) { return "csrrs "; }
            if (i.funct3 == 0b011) { return "csrrc "; }
            if (i.funct3 == 0b101) { return "csrrwi"; }
            if (i.funct3 == 0b110) { return "csrrsi"; }
            if (i.funct3 == 0b111) { return "csrrci"; }
        }
        return Rv32im::mnemonic();
    }
};
