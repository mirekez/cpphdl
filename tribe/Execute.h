#pragma once

#include "Config.h"

using namespace cpphdl;

class Execute: public Module
{
public:
    _PORT(State)  state_in;

    _PORT(uint32_t) alu_result_out      = _ASSIGN( (uint32_t)alu_result_comb_func() );
    _PORT(uint32_t) debug_alu_a_out     = _ASSIGN_COMB( alu_a_comb_func() );
    _PORT(uint32_t) debug_alu_b_out     = _ASSIGN_COMB( alu_b_comb_func() );
    _PORT(bool)     branch_taken_out    = _ASSIGN_COMB( branch_taken_comb_func() );
    _PORT(uint32_t) branch_target_out   = _ASSIGN_COMB( branch_target_comb_func() );

private:

    _LAZY_COMB(alu_a_comb, uint32_t)
        return alu_a_comb = state_in().rs1_val;
    }

    _LAZY_COMB(alu_b_comb, uint32_t)

        return alu_b_comb = (state_in().alu_op == Alu::ADD && state_in().mem_op != Mem::MNONE) ?
                                               uint32_t(state_in().imm) :      // load/store address calc uses imm
                            (state_in().br_op != Br::BNONE || state_in().rs2) ?
                                state_in().rs2_val : uint32_t(state_in().imm);
    }

    _LAZY_COMB(alu_result_comb, uint64_t)

        uint32_t a;
        uint32_t b;
        uint32_t alu_op;
        a = alu_a_comb_func();
        b = alu_b_comb_func();
        alu_result_comb = 0;
        alu_op = state_in().alu_op;
        switch (alu_op) {
            case Alu::ADD:  alu_result_comb = a + b;                              break;
            case Alu::SUB:  alu_result_comb = a - b;                              break;
            case Alu::AND:  alu_result_comb = a & b;                              break;
            case Alu::OR:   alu_result_comb = a | b;                              break;
            case Alu::XOR:  alu_result_comb = a ^ b;                              break;
            case Alu::SLL:  alu_result_comb = a << (b & 0x1F);                    break;
            case Alu::SRL:  alu_result_comb = a >> (b & 0x1F);                    break;
            case Alu::SRA:  alu_result_comb = uint32_t(int32_t(a) >> (b & 0x1F)); break;
            case Alu::SLT:  alu_result_comb = (int32_t(a) < int32_t(b));          break;
            case Alu::SLTU: alu_result_comb = (a < b);                            break;
            case Alu::PASS: alu_result_comb = b;                                  break;
            case Alu::MUL:  alu_result_comb = a * b;                              break;
            case Alu::MULH: alu_result_comb = (uint64_t(int64_t(int32_t(a)) * int64_t(int32_t(b))) >> 32); break;
            case Alu::MULHSU: alu_result_comb = (uint64_t(int64_t(int32_t(a)) * int64_t(uint64_t(b))) >> 32); break;
            case Alu::MULHU: alu_result_comb = ((uint64_t)a * b) >> 32;           break;
            case Alu::DIV:  alu_result_comb = b == 0 ? ~0u : (a == 0x80000000u && b == 0xffffffffu ? a : uint32_t(int32_t(a) / int32_t(b))); break;
            case Alu::DIVU: alu_result_comb = b == 0 ? ~0u : a / b;              break;
            case Alu::REM:  alu_result_comb = b == 0 ? a : (a == 0x80000000u && b == 0xffffffffu ? 0 : uint32_t(int32_t(a) % int32_t(b))); break;
            case Alu::REMU: alu_result_comb = b == 0 ? a : a % b;                break;
            case Alu::ANONE:                                                      break;
        }
        if (alu_op == Alu::SLT || alu_op == Alu::SLTU) {
            alu_result_comb |= ((uint64_t)(a == b))<<32;
        }

        return alu_result_comb;
    }

    _LAZY_COMB(branch_taken_comb, bool)

        uint64_t alu_result;
        alu_result = alu_result_comb_func();
        branch_taken_comb = false;
        switch (state_in().br_op) {
            case Br::BEQZ: branch_taken_comb = alu_result>>32;              break;
            case Br::BNEZ: branch_taken_comb = !(alu_result>>32);           break;
            case Br::BEQ: branch_taken_comb = alu_result>>32;               break;
            case Br::BNE: branch_taken_comb = !(alu_result>>32);            break;
            case Br::BLT: branch_taken_comb = alu_result&0xFFFFFFFFu;       break;
            case Br::BGE: branch_taken_comb = !(alu_result&0xFFFFFFFFu);    break;
            case Br::BLTU: branch_taken_comb = alu_result&0xFFFFFFFFu;      break;
            case Br::BGEU: branch_taken_comb = !(alu_result&0xFFFFFFFFu);   break;
            case Br::JAL: branch_taken_comb = true;                         break;
            case Br::JALR: branch_taken_comb = true;                        break;
            case Br::JR: branch_taken_comb = true;                          break;
            case Br::BNONE: break;
        }
        return branch_taken_comb = branch_taken_comb && state_in().valid;
    }

    _LAZY_COMB(branch_target_comb, uint32_t)

        branch_target_comb = 0;
        if (state_in().br_op != Br::BNONE)
        {
            if (state_in().br_op == Br::JAL) {
                branch_target_comb = state_in().pc + state_in().imm;
            }
            else if (state_in().br_op == Br::JALR || state_in().br_op == Br::JR) {
                branch_target_comb = (state_in().rs1_val + state_in().imm) & ~1U;
            }
            else {     // conditional branch
                branch_target_comb = state_in().pc + state_in().imm;
            }
        }
        return branch_target_comb;
    }

public:

    void _work(bool reset)
    {
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
    }

    void _assign()
    {
    }

};
