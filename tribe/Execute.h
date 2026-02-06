#pragma once

using namespace cpphdl;

class Execute: public Module
{
public:
    __PORT(State)  state_in;

    __PORT(bool)      mem_write_out      = __VAR( mem_write_reg );
    __PORT(uint32_t)  mem_write_addr_out = __VAR( mem_addr_reg );
    __PORT(uint32_t)  mem_write_data_out = __VAR( mem_data_reg );
    __PORT(uint8_t)   mem_write_mask_out = __VAR( mem_mask_reg );
    __PORT(bool)      mem_read_out       = __VAR( mem_read_reg );
    __PORT(uint32_t)  mem_read_addr_out  = __VAR( mem_addr_reg );

    __PORT(uint32_t) alu_result_out      = __EXPR( (uint32_t)alu_result_comb_func() );
    __PORT(uint32_t) debug_alu_a_out     = __VAR( alu_a_comb_func() );
    __PORT(uint32_t) debug_alu_b_out     = __VAR( alu_b_comb_func() );
    __PORT(bool)     branch_taken_out    = __VAR( branch_taken_comb_func() );
    __PORT(uint32_t) branch_target_out   = __VAR( branch_target_comb_func() );

private:

    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8>  mem_mask_reg;
    reg<u1>  mem_write_reg;
    reg<u1>  mem_read_reg;

    __LAZY_COMB(alu_a_comb, uint32_t)
        return alu_a_comb = state_in().rs1_val;
    }

    __LAZY_COMB(alu_b_comb, uint32_t)

        return alu_b_comb = (state_in().alu_op == Alu::ADD && state_in().mem_op != Mem::MNONE) ?
                                               uint32_t(state_in().imm) :      // load/store address calc uses imm
                            (state_in().rs2 || state_in().br_op == BEQZ || state_in().br_op == BNEZ) ?
                                state_in().rs2_val : uint32_t(state_in().imm);
    }

    __LAZY_COMB(alu_result_comb, uint64_t)

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
            case Alu::MULH: alu_result_comb = ((uint64_t)a * b) >> 32;            break;
            case Alu::DIV:  alu_result_comb = a / b;                              break;
            case Alu::REM:  alu_result_comb = a % b;                              break;
            case Alu::ANONE:                                                      break;
        }
        if (alu_op == Alu::SLT || alu_op == Alu::SLTU) {
            alu_result_comb |= ((uint64_t)(a == b))<<32;
        }

        return alu_result_comb;
    }

    __LAZY_COMB(branch_taken_comb, bool)

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

    __LAZY_COMB(branch_target_comb, uint32_t)

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

    void do_memory()
    {
        mem_addr_reg.next = alu_result_comb_func();
        mem_data_reg.next = state_in().rs2_val;

        mem_write_reg.next = 0;
        mem_mask_reg.next = 0;
        if (state_in().mem_op == Mem::STORE && state_in().valid) {  // parallel case, full case
            switch (state_in().funct3)
            {
                case 0b000: // LB
                    mem_write_reg.next = state_in().valid;
                    mem_mask_reg.next = 0x1;
                    break;
                case 0b001: // LH
                    mem_write_reg.next = state_in().valid;
                    mem_mask_reg.next = 0x3;
                    break;
                case 0b010: // LW
                    mem_write_reg.next = state_in().valid;
                    mem_mask_reg.next = 0xF;
                    break;
            }
        }

        mem_read_reg.next = 0;
        if (state_in().mem_op == Mem::LOAD && state_in().valid)
        {
            switch (state_in().funct3)
            {
                case 0b000: mem_read_reg.next = 1; break;
                case 0b001: mem_read_reg.next = 1; break;
                case 0b010: mem_read_reg.next = 1; break;
                case 0b100: mem_read_reg.next = 1; break;
                case 0b101: mem_read_reg.next = 1; break;
                default: break;
            }
        }
    }

public:

    void _work(bool reset)
    {
        do_memory();

        if (reset) {
            mem_write_reg.clr();
            mem_read_reg.clr();
        }
    }

    void _strobe()
    {
        mem_addr_reg.strobe();
        mem_data_reg.strobe();
        mem_mask_reg.strobe();
        mem_write_reg.strobe();
        mem_read_reg.strobe();
    }

    void _connect()
    {
    }

};
