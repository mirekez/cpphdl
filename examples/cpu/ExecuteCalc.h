#pragma once

using namespace cpphdl;

template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class ExecuteCalc: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

    struct State
    {
        uint32_t alu_result;
    };

    bool     branch_taken_comb;
    uint32_t branch_target_comb;
    uint32_t alu_result_comb;

    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8>  mem_mask_reg;
    reg<u1>  mem_write_reg;
    reg<u1>  mem_read_reg;

public:
    __PORT(bool)      mem_write_out      = __VAL( mem_write_reg );
    __PORT(uint32_t)  mem_write_addr_out = __VAL( mem_addr_reg );
    __PORT(uint32_t)  mem_write_data_out = __VAL( mem_data_reg );
    __PORT(uint8_t)   mem_write_mask_out = __VAL( mem_mask_reg );
    __PORT(bool)      mem_read_out       = __VAL( mem_read_reg );
    __PORT(uint32_t)  mem_read_addr_out  = __VAL( mem_addr_reg );

    __PORT(uint32_t)  alu_result_out    = __VAL( alu_result_comb_func() );
    __PORT(bool)      branch_taken_out  = __VAL( branch_taken_comb_func() );
    __PORT(uint32_t)  branch_target_out = __VAL( branch_target_comb_func() );

public:

    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }

    uint32_t alu_result_comb_func()
    {
        uint32_t a = state_in()[ID-1].rs1_val;
        uint32_t b = (state_in()[ID-1].alu_op == Alu::ADD && state_in()[ID-1].mem_op != Mem::MNONE)
                        ? uint32_t(state_in()[ID-1].imm)      // load/store address calc uses imm
                        : state_in()[ID-1].rs2 ? state_in()[ID-1].rs2_val : uint32_t(state_in()[ID-1].imm);
        switch (state_in()[ID-1].alu_op) {
            case Alu::ADD:  alu_result_comb = a + b; break;
            case Alu::SUB:  alu_result_comb = a - b; break;
            case Alu::AND:  alu_result_comb = a & b; break;
            case Alu::OR:   alu_result_comb = a | b; break;
            case Alu::XOR:  alu_result_comb = a ^ b; break;
            case Alu::SLL:  alu_result_comb = a << (b & 0x1F); break;
            case Alu::SRL:  alu_result_comb = a >> (b & 0x1F); break;
            case Alu::SRA:  alu_result_comb = uint32_t(int32_t(a) >> (b & 0x1F)); break;
            case Alu::SLT:  alu_result_comb = (int32_t(a) < int32_t(b)); break;
            case Alu::SLTU: alu_result_comb = (a < b); break;
            case Alu::PASSA: alu_result_comb = a; break;
            case Alu::PASSB: alu_result_comb = b; break;
            case Alu::MUL:  alu_result_comb = a * b; break;
            case Alu::ANONE: break;
        }
        return alu_result_comb;
    }

    bool branch_taken_comb_func()
    {
        branch_taken_comb = false;
        switch (state_in()[ID-1].br_op) {
            case Br::BEQ: branch_taken_comb = (state_in()[ID-1].rs1_val == state_in()[ID-1].rs2_val); break;
            case Br::BNE: branch_taken_comb = (state_in()[ID-1].rs1_val != state_in()[ID-1].rs2_val); break;
            case Br::BLT: branch_taken_comb = (int32_t(state_in()[ID-1].rs1_val) < int32_t(state_in()[ID-1].rs2_val)); break;
            case Br::BGE: branch_taken_comb = (int32_t(state_in()[ID-1].rs1_val) >= int32_t(state_in()[ID-1].rs2_val)); break;
            case Br::BLTU: branch_taken_comb = (state_in()[ID-1].rs1_val < state_in()[ID-1].rs2_val); break;
            case Br::BGEU: branch_taken_comb = (state_in()[ID-1].rs1_val >= state_in()[ID-1].rs2_val); break;
            case Br::JAL: branch_taken_comb = true; break;
            case Br::JALR: branch_taken_comb = true; break;
            case Br::JR: branch_taken_comb = true; break;
            case Br::BNONE: break;
        }
        return branch_taken_comb && state_in()[ID-1].valid;
    }

    uint32_t branch_target_comb_func()
    {
        branch_target_comb = 0;
        if (state_in()[ID-1].br_op != Br::BNONE)
        {
            if (state_in()[ID-1].br_op == Br::JAL) {
                branch_target_comb = state_in()[ID-1].pc + state_in()[ID-1].imm;
            }
            else if (state_in()[ID-1].br_op == Br::JALR || state_in()[ID-1].br_op == Br::JR) {
                branch_target_comb = (state_in()[ID-1].rs1_val + state_in()[ID-1].imm) & ~1U;
            }
            else {     // conditional branch
                branch_target_comb = state_in()[ID-1].pc + state_in()[ID-1].imm;
            }
        }
        return branch_target_comb;
    }

    void do_execute()
    {
        state_reg.next[0].alu_result = alu_result_comb_func();
    }

    void start_memory()
    {
        mem_addr_reg.next = alu_result_comb_func();
        mem_data_reg.next = state_in()[ID-1].rs2_val;

        mem_write_reg.next = 0;
        mem_mask_reg.next = 0;
        if (state_in()[ID-1].mem_op == Mem::STORE && state_in()[ID-1].valid) {  // parallel case, full case
            switch (state_in()[ID-1].funct3)
            {
                case 0b000: // LB
                    mem_write_reg.next = state_in()[ID-1].valid;
                    mem_mask_reg.next = 0x1;
                    break;
                case 0b001: // LH
                    mem_write_reg.next = state_in()[ID-1].valid;
                    mem_mask_reg.next = 0x3;
                    break;
                case 0b010: // LW
                    mem_write_reg.next = state_in()[ID-1].valid;
                    mem_mask_reg.next = 0xF;
                    break;
            }
        }

        mem_read_reg.next = 0;
        if (state_in()[ID-1].mem_op == Mem::LOAD && state_in()[ID-1].valid)
        {
            switch (state_in()[ID-1].funct3)
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

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        if (reset) {
            mem_write_reg.clr();
            mem_read_reg.clr();
        }
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::work(clk, reset);  // first because it copies all registers from previous stage
        state_reg.next[0] = {};
        do_execute();
        start_memory();
    }

    void strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::strobe();
        mem_addr_reg.strobe();
        mem_data_reg.strobe();
        mem_mask_reg.strobe();
        mem_write_reg.strobe();
        mem_read_reg.strobe();
    }
};
