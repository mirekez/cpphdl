#pragma once

#include "Rv32ia.h"

#ifdef ENABLE_RV32IA
#define PREV_SPEC Rv32ia
#else
#define PREV_SPEC Rv32im
#endif

struct Zicsr: public PREV_SPEC
{
    void decode(State& state_out)
    {
        state_out = {};

        PREV_SPEC::decode(state_out);

        if ((raw & 3) == 3 && r.opcode == 0b1110011 && i.funct3 == 0) {
            if (raw == 0x00000073) {
                state_out = {};
                state_out.sys_op = Sys::ECALL;
                state_out.imm = raw;
                state_out.br_op = Br::JR;
            }
            else if (raw == 0x00100073) {
                state_out = {};
                state_out.sys_op = Sys::EBREAK;
                state_out.trap_op = Trap::BREAKPOINT;
                state_out.imm = raw;
                state_out.br_op = Br::JR;
            }
            else if (raw == 0x30200073) {
                state_out = {};
                state_out.sys_op = Sys::MRET;
                state_out.imm = raw;
                state_out.br_op = Br::JR;
            }
            else if (raw == 0x10200073) {
                state_out = {};
                state_out.sys_op = Sys::SRET;
                state_out.imm = raw;
                state_out.br_op = Br::JR;
            }
            else if (raw == 0x10500073) {
                state_out = {};
                state_out.sys_op = Sys::WFI;
                state_out.imm = raw;
            }
#ifdef ENABLE_MMU_TLB
            else if ((raw & 0xfe007fff) == 0x12000073) {
                state_out = {};
                state_out.sys_op = Sys::SFENCE_VMA;
                state_out.rs1 = r.rs1;
                state_out.rs2 = r.rs2;
                state_out.imm = raw;
            }
#endif
            else {
                state_out = {};
                state_out.sys_op = Sys::TRAP;
                state_out.trap_op = Trap::ILLEGAL_INST;
                state_out.imm = raw;
                state_out.br_op = Br::JR;
            }
        }

        if ((raw & 3) == 3 && r.opcode == 0b1110011 && i.funct3 != 0) {
            state_out = {};
            state_out.rd = i.rd;
            state_out.funct3 = i.funct3;
            state_out.csr_addr = i.imm11_0;
            state_out.csr_imm = i.rs1;
            state_out.imm = raw;

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
                    state_out.sys_op = Sys::TRAP;
                    state_out.trap_op = Trap::ILLEGAL_INST;
                    state_out.imm = raw;
                    state_out.br_op = Br::JR;
                    break;
            }
        }
    }

    std::string mnemonic()
    {
        if ((raw & 3) == 3 && r.opcode == 0b1110011) {
            if (raw == 0x00000073) { return "ecall "; }
            if (raw == 0x00100073) { return "ebreak"; }
            if (raw == 0x30200073) { return "mret  "; }
            if (raw == 0x10200073) { return "sret  "; }
            if (raw == 0x10500073) { return "wfi   "; }
#ifdef ENABLE_MMU_TLB
            if ((raw & 0xfe007fff) == 0x12000073) { return "sfence"; }
#endif
            if (i.funct3 == 0b001) { return "csrrw "; }
            if (i.funct3 == 0b010) { return "csrrs "; }
            if (i.funct3 == 0b011) { return "csrrc "; }
            if (i.funct3 == 0b101) { return "csrrwi"; }
            if (i.funct3 == 0b110) { return "csrrsi"; }
            if (i.funct3 == 0b111) { return "csrrci"; }
        }
        return PREV_SPEC::mnemonic();
    }
};
