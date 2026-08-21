#pragma once

#include "Config.h"

using namespace cpphdl;

class Execute: public Module
{
public:
    _PORT(State)  state_in;
    // Raw architectural execute state used to identify a multi-cycle divide.
    // This is deliberately separate from state_in, which may be rewritten for
    // trap redirection and would otherwise create a wait/interrupt feedback path.
    _PORT(State)  multicycle_state_in;

    _PORT(uint32_t) alu_result_out      = _ASSIGN( (uint32_t)alu_result_comb_func() );
    _PORT(uint32_t) debug_alu_a_out     = _ASSIGN_COMB( alu_a_comb_func() );
    _PORT(uint32_t) debug_alu_b_out     = _ASSIGN_COMB( alu_b_comb_func() );
    _PORT(bool)     branch_taken_out    = _ASSIGN_COMB( branch_taken_comb_func() );
    _PORT(uint32_t) branch_target_out   = _ASSIGN_COMB( branch_target_comb_func() );
    _PORT(bool)     multicycle_wait_out = _ASSIGN_COMB( multicycle_wait_comb_func() );

private:

    reg<u1> div_busy_reg;
    reg<u1> div_done_reg;
    reg<u<6>> div_count_reg;
    reg<u32> div_dividend_reg;
    reg<u32> div_divisor_reg;
    reg<u32> div_quotient_reg;
    reg<u<33>> div_remainder_reg;
    reg<u1> div_quotient_neg_reg;
    reg<u1> div_remainder_neg_reg;
    reg<u32> div_result_reg;
    reg<u32> div_pc_reg;
    reg<u32> div_a_reg;
    reg<u32> div_b_reg;
    reg<u8> div_op_reg;

    reg<u1> mul_busy_reg;
    reg<u1> mul_done_reg;
    reg<u<6>> mul_count_reg;
    reg<u64> mul_multiplicand_reg;
    reg<u32> mul_multiplier_reg;
    reg<u64> mul_accumulator_reg;
    reg<u1> mul_negate_reg;
    reg<u1> mul_high_reg;
    reg<u32> mul_result_reg;
    reg<u32> mul_pc_reg;
    reg<u32> mul_a_reg;
    reg<u32> mul_b_reg;
    reg<u8> mul_op_reg;

    _LAZY_COMB(div_instruction_comb, bool)
        uint32_t op;
        op = multicycle_state_in().alu_op;
        return div_instruction_comb = multicycle_state_in().valid &&
            (op == Alu::DIV || op == Alu::DIVU ||
             op == Alu::REM || op == Alu::REMU);
    }

    _LAZY_COMB(div_result_match_comb, bool)
        return div_result_match_comb = div_done_reg && div_instruction_comb_func() &&
            div_pc_reg == multicycle_state_in().pc &&
            div_a_reg == multicycle_state_in().rs1_val &&
            div_b_reg == multicycle_state_in().rs2_val &&
            div_op_reg == multicycle_state_in().alu_op;
    }

    _LAZY_COMB(mul_instruction_comb, bool)
        uint32_t op;
        op = multicycle_state_in().alu_op;
        return mul_instruction_comb = multicycle_state_in().valid &&
            (op == Alu::MUL || op == Alu::MULH ||
             op == Alu::MULHSU || op == Alu::MULHU);
    }

    _LAZY_COMB(mul_result_match_comb, bool)
        return mul_result_match_comb = mul_done_reg && mul_instruction_comb_func() &&
            mul_pc_reg == multicycle_state_in().pc &&
            mul_a_reg == multicycle_state_in().rs1_val &&
            mul_b_reg == multicycle_state_in().rs2_val &&
            mul_op_reg == multicycle_state_in().alu_op;
    }

    _LAZY_COMB(multicycle_wait_comb, bool)
        return multicycle_wait_comb =
            (div_instruction_comb_func() && !div_result_match_comb_func()) ||
            (mul_instruction_comb_func() && !mul_result_match_comb_func());
    }

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
            case Alu::MUL:  alu_result_comb = mul_result_reg;                    break;
            case Alu::MULH: alu_result_comb = mul_result_reg;                    break;
            case Alu::MULHSU: alu_result_comb = mul_result_reg;                  break;
            case Alu::MULHU: alu_result_comb = mul_result_reg;                   break;
            case Alu::DIV:  alu_result_comb = div_result_reg;                    break;
            case Alu::DIVU: alu_result_comb = div_result_reg;                    break;
            case Alu::REM:  alu_result_comb = div_result_reg;                    break;
            case Alu::REMU: alu_result_comb = div_result_reg;                    break;
            case Alu::ANONE:                                                      break;
        }
        return alu_result_comb;
    }

    _LAZY_COMB(branch_taken_comb, bool)

        uint32_t a;
        uint32_t b;
        bool signed_less;
        a = alu_a_comb_func();
        b = alu_b_comb_func();
        // Express signed comparison using only unsigned operations.  Some
        // SystemVerilog tools make a mixed signed/unsigned relational
        // expression unsigned; in particular, the CppHDL translation of
        // int32_t(a) >= int32_t(b) previously became `a >= signed'(b)` and
        // misclassified every negative `a` as greater than zero.
        signed_less = ((a ^ b) >> 31) != 0 ? ((a >> 31) != 0) : a < b;
        branch_taken_comb = false;
        switch (state_in().br_op) {
            case Br::BEQZ: branch_taken_comb = a == 0;                      break;
            case Br::BNEZ: branch_taken_comb = a != 0;                      break;
            case Br::BEQ: branch_taken_comb = a == b;                       break;
            case Br::BNE: branch_taken_comb = a != b;                       break;
            case Br::BLT: branch_taken_comb = signed_less;                  break;
            case Br::BGE: branch_taken_comb = !signed_less;                 break;
            case Br::BLTU: branch_taken_comb = a < b;                       break;
            case Br::BGEU: branch_taken_comb = a >= b;                      break;
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
        uint32_t a;
        uint32_t b;
        uint32_t op;
        uint32_t abs_a;
        uint32_t abs_b;
        uint64_t shifted_remainder;
        uint32_t shifted_quotient;
        uint32_t final_result;
        bool signed_op;
        bool quotient_op;
        bool a_negative;
        bool b_negative;
        bool mul_signed_a;
        bool mul_signed_b;
        bool mul_a_negative;
        bool mul_b_negative;
        uint32_t mul_abs_a;
        uint32_t mul_abs_b;
        uint64_t mul_sum;
        uint64_t mul_product;

        a = multicycle_state_in().rs1_val;
        b = multicycle_state_in().rs2_val;
        op = multicycle_state_in().alu_op;
        signed_op = op == Alu::DIV || op == Alu::REM;
        quotient_op = op == Alu::DIV || op == Alu::DIVU;
        a_negative = signed_op && ((a >> 31) != 0);
        b_negative = signed_op && ((b >> 31) != 0);
        abs_a = a_negative ? (~a + 1u) : a;
        abs_b = b_negative ? (~b + 1u) : b;
        mul_signed_a = op == Alu::MULH || op == Alu::MULHSU;
        mul_signed_b = op == Alu::MULH;
        mul_a_negative = mul_signed_a && ((a >> 31) != 0);
        mul_b_negative = mul_signed_b && ((b >> 31) != 0);
        mul_abs_a = mul_a_negative ? (~a + 1u) : a;
        mul_abs_b = mul_b_negative ? (~b + 1u) : b;

        if (div_busy_reg) {
            if (div_count_reg == 32) {
                // Final sign correction gets its own cycle.  Combining it with
                // the 32nd restoring subtract creates two serial carry chains.
                final_result = (div_op_reg == Alu::DIV || div_op_reg == Alu::DIVU) ?
                    (uint32_t)div_quotient_reg : (uint32_t)div_remainder_reg;
                if (((div_op_reg == Alu::DIV || div_op_reg == Alu::DIVU) &&
                     div_quotient_neg_reg) ||
                    ((div_op_reg == Alu::REM || div_op_reg == Alu::REMU) &&
                     div_remainder_neg_reg)) {
                    final_result = ~final_result + 1u;
                }
                div_result_reg._next = final_result;
                div_busy_reg._next = false;
                div_done_reg._next = true;
            }
            else {
                shifted_remainder = ((uint64_t)div_remainder_reg << 1) |
                    ((uint32_t)div_dividend_reg >> 31);
                shifted_quotient = (uint32_t)div_quotient_reg << 1;
                if (shifted_remainder >= (uint32_t)div_divisor_reg) {
                    shifted_remainder -= (uint32_t)div_divisor_reg;
                    shifted_quotient |= 1u;
                }
                div_dividend_reg._next = (uint32_t)div_dividend_reg << 1;
                div_remainder_reg._next = shifted_remainder;
                div_quotient_reg._next = shifted_quotient;
                div_count_reg._next = div_count_reg + 1;
            }
        }
        else if (div_instruction_comb_func() && !div_result_match_comb_func()) {
            div_pc_reg._next = multicycle_state_in().pc;
            div_a_reg._next = a;
            div_b_reg._next = b;
            div_op_reg._next = op;
            div_count_reg._next = 0;
            div_dividend_reg._next = abs_a;
            div_divisor_reg._next = abs_b;
            div_quotient_reg._next = 0;
            div_remainder_reg._next = 0;
            div_quotient_neg_reg._next = a_negative != b_negative;
            div_remainder_neg_reg._next = a_negative;
            if (b == 0) {
                div_result_reg._next = quotient_op ? ~0u : a;
                div_busy_reg._next = false;
                div_done_reg._next = true;
            }
            else {
                div_busy_reg._next = true;
                div_done_reg._next = false;
            }
        }
        else if (!div_instruction_comb_func()) {
            div_done_reg._next = false;
        }

        if (mul_busy_reg) {
            mul_sum = mul_accumulator_reg;
            if (((uint32_t)mul_multiplier_reg & 1u) != 0) {
                mul_sum += (uint64_t)mul_multiplicand_reg;
            }
            mul_accumulator_reg._next = mul_sum;
            mul_multiplicand_reg._next = (uint64_t)mul_multiplicand_reg << 1;
            mul_multiplier_reg._next = (uint32_t)mul_multiplier_reg >> 1;
            if (mul_count_reg == 31) {
                mul_product = mul_negate_reg ? (~mul_sum + 1u) : mul_sum;
                mul_result_reg._next = mul_high_reg ?
                    (uint32_t)(mul_product >> 32) : (uint32_t)mul_product;
                mul_busy_reg._next = false;
                mul_done_reg._next = true;
            }
            else {
                mul_count_reg._next = mul_count_reg + 1;
            }
        }
        else if (mul_instruction_comb_func() && !mul_result_match_comb_func()) {
            mul_pc_reg._next = multicycle_state_in().pc;
            mul_a_reg._next = a;
            mul_b_reg._next = b;
            mul_op_reg._next = op;
            mul_count_reg._next = 0;
            mul_multiplicand_reg._next = mul_abs_a;
            mul_multiplier_reg._next = mul_abs_b;
            mul_accumulator_reg._next = 0;
            mul_negate_reg._next = mul_a_negative != mul_b_negative;
            mul_high_reg._next = op != Alu::MUL;
            mul_busy_reg._next = true;
            mul_done_reg._next = false;
        }
        else if (!mul_instruction_comb_func()) {
            mul_done_reg._next = false;
        }

        if (reset) {
            div_busy_reg.clr();
            div_done_reg.clr();
            div_count_reg.clr();
            div_dividend_reg.clr();
            div_divisor_reg.clr();
            div_quotient_reg.clr();
            div_remainder_reg.clr();
            div_quotient_neg_reg.clr();
            div_remainder_neg_reg.clr();
            div_result_reg.clr();
            div_pc_reg.clr();
            div_a_reg.clr();
            div_b_reg.clr();
            div_op_reg.clr();
            mul_busy_reg.clr();
            mul_done_reg.clr();
            mul_count_reg.clr();
            mul_multiplicand_reg.clr();
            mul_multiplier_reg.clr();
            mul_accumulator_reg.clr();
            mul_negate_reg.clr();
            mul_high_reg.clr();
            mul_result_reg.clr();
            mul_pc_reg.clr();
            mul_a_reg.clr();
            mul_b_reg.clr();
            mul_op_reg.clr();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        div_busy_reg.strobe(checkpoint_fd);
        div_done_reg.strobe(checkpoint_fd);
        div_count_reg.strobe(checkpoint_fd);
        div_dividend_reg.strobe(checkpoint_fd);
        div_divisor_reg.strobe(checkpoint_fd);
        div_quotient_reg.strobe(checkpoint_fd);
        div_remainder_reg.strobe(checkpoint_fd);
        div_quotient_neg_reg.strobe(checkpoint_fd);
        div_remainder_neg_reg.strobe(checkpoint_fd);
        div_result_reg.strobe(checkpoint_fd);
        div_pc_reg.strobe(checkpoint_fd);
        div_a_reg.strobe(checkpoint_fd);
        div_b_reg.strobe(checkpoint_fd);
        div_op_reg.strobe(checkpoint_fd);
        mul_busy_reg.strobe(checkpoint_fd);
        mul_done_reg.strobe(checkpoint_fd);
        mul_count_reg.strobe(checkpoint_fd);
        mul_multiplicand_reg.strobe(checkpoint_fd);
        mul_multiplier_reg.strobe(checkpoint_fd);
        mul_accumulator_reg.strobe(checkpoint_fd);
        mul_negate_reg.strobe(checkpoint_fd);
        mul_high_reg.strobe(checkpoint_fd);
        mul_result_reg.strobe(checkpoint_fd);
        mul_pc_reg.strobe(checkpoint_fd);
        mul_a_reg.strobe(checkpoint_fd);
        mul_b_reg.strobe(checkpoint_fd);
        mul_op_reg.strobe(checkpoint_fd);
    }

    void _assign()
    {
    }

};
