#pragma once

#include "Rv32im.h"

struct Rv32ia: public Rv32im
{
    static constexpr uint8_t FUNCT5_AMOADD  = 0b00000;
    static constexpr uint8_t FUNCT5_AMOSWAP = 0b00001;
    static constexpr uint8_t FUNCT5_LR      = 0b00010;
    static constexpr uint8_t FUNCT5_SC      = 0b00011;
    static constexpr uint8_t FUNCT5_AMOXOR  = 0b00100;
    static constexpr uint8_t FUNCT5_AMOOR   = 0b01000;
    static constexpr uint8_t FUNCT5_AMOAND  = 0b01100;
    static constexpr uint8_t FUNCT5_AMOMIN  = 0b10000;
    static constexpr uint8_t FUNCT5_AMOMAX  = 0b10100;
    static constexpr uint8_t FUNCT5_AMOMINU = 0b11000;
    static constexpr uint8_t FUNCT5_AMOMAXU = 0b11100;

    uint8_t funct5()
    {
        return (uint8_t)(raw >> 27);
    }

    uint8_t aq()
    {
        return (uint8_t)((raw >> 26) & 1u);
    }

    uint8_t rl()
    {
        return (uint8_t)((raw >> 25) & 1u);
    }

    void decode(State& state_out)
    {
        state_out = {};

        Rv32im::decode(state_out);

        if ((raw & 3) == 3 && r.opcode == 0b0101111 && r.funct3 == 0b010) {
            state_out.rd = r.rd;
            state_out.rs1 = r.rs1;
            state_out.rs2 = r.rs2;
            state_out.funct3 = r.funct3;
            state_out.imm = 0;
            state_out.alu_op = Alu::ADD;

            switch (funct5()) {
                case FUNCT5_LR:
                    if (r.rs2 == 0) {
                        state_out.amo_op = Amo::LR_W;
                        state_out.mem_op = Mem::LOAD;
                        state_out.wb_op = Wb::MEM;
                    }
                    break;
                case FUNCT5_SC:
                    state_out.amo_op = Amo::SC_W;
                    state_out.mem_op = Mem::STORE;
                    state_out.wb_op = Wb::ALU;
                    break;
                case FUNCT5_AMOSWAP:
                    state_out.amo_op = Amo::AMOSWAP_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOADD:
                    state_out.amo_op = Amo::AMOADD_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOXOR:
                    state_out.amo_op = Amo::AMOXOR_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOAND:
                    state_out.amo_op = Amo::AMOAND_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOOR:
                    state_out.amo_op = Amo::AMOOR_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOMIN:
                    state_out.amo_op = Amo::AMOMIN_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOMAX:
                    state_out.amo_op = Amo::AMOMAX_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOMINU:
                    state_out.amo_op = Amo::AMOMINU_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                case FUNCT5_AMOMAXU:
                    state_out.amo_op = Amo::AMOMAXU_W;
                    state_out.mem_op = Mem::LOAD;
                    state_out.wb_op = Wb::MEM;
                    break;
                default:
                    state_out = {};
                    break;
            }
        }
    }

    std::string mnemonic()
    {
        if ((raw & 3) == 3 && r.opcode == 0b0101111 && r.funct3 == 0b010) {
            if (funct5() == FUNCT5_LR && r.rs2 == 0) { return "lr.w  "; }
            if (funct5() == FUNCT5_SC) { return "sc.w  "; }
            if (funct5() == FUNCT5_AMOSWAP) { return "amoswp"; }
            if (funct5() == FUNCT5_AMOADD) { return "amoadd"; }
            if (funct5() == FUNCT5_AMOXOR) { return "amoxor"; }
            if (funct5() == FUNCT5_AMOAND) { return "amoand"; }
            if (funct5() == FUNCT5_AMOOR) { return "amoor "; }
            if (funct5() == FUNCT5_AMOMIN) { return "amomin"; }
            if (funct5() == FUNCT5_AMOMAX) { return "amomax"; }
            if (funct5() == FUNCT5_AMOMINU) { return "amomiu"; }
            if (funct5() == FUNCT5_AMOMAXU) { return "amomau"; }
        }
        return Rv32im::mnemonic();
    }
};
