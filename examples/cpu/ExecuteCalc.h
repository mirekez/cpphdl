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

public:
    uint32_t                  *alu_result_out = &alu_result_comb;
    bool                      *branch_taken_out = &branch_taken_comb;
    uint32_t                  *branch_target_out = &branch_target_comb;

    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }

    bool branch_taken_comb_func()
    {
        branch_taken_comb = false;
        switch ((*state_in)[ID-1].br_op) {
            case Br::BEQ: branch_taken_comb = ((*state_in)[ID-1].rs1_val == (*state_in)[ID-1].rs2_val); break;
            case Br::BNE: branch_taken_comb = ((*state_in)[ID-1].rs1_val != (*state_in)[ID-1].rs2_val); break;
            case Br::BLT: branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) < int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BGE: branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) >= int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BLTU: branch_taken_comb = ((*state_in)[ID-1].rs1_val < (*state_in)[ID-1].rs2_val); break;
            case Br::BGEU: branch_taken_comb = ((*state_in)[ID-1].rs1_val >= (*state_in)[ID-1].rs2_val); break;
            case Br::JAL: branch_taken_comb = true; break;
            case Br::JALR: branch_taken_comb = true; break;
            case Br::BNONE: break;
        }
        return branch_taken_comb && (*state_in)[ID-1].valid;
    }

    bool branch_target_comb_func()
    {
        branch_target_comb = 0;
        if ((*state_in)[ID-1].br_op != Br::BNONE)
        {
            if ((*state_in)[ID-1].br_op == Br::JAL)
                branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
            else if ((*state_in)[ID-1].br_op == Br::JALR)
                branch_target_comb = ((*state_in)[ID-1].rs1_val + (*state_in)[ID-1].imm) & ~1u;
            else     // conditional branch
                branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
        }
        return branch_target_comb;
    }

    uint32_t alu_result_comb_func()
    {
        uint32_t a = (*state_in)[ID-1].rs1_val;
        uint32_t b = ((*state_in)[ID-1].alu_op == Alu::ADD && (*state_in)[ID-1].mem_op != Mem::MNONE)
                        ? uint32_t((*state_in)[ID-1].imm)      // load/store address calc uses imm
                        : (*state_in)[ID-1].rs2 ? (*state_in)[ID-1].rs2_val : uint32_t((*state_in)[ID-1].imm);

        switch ((*state_in)[ID-1].alu_op) {
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
            case Alu::PASS_RS1: alu_result_comb = a; break;
            case Alu::ANONE: break;
        }
        return alu_result_comb;
    }

    void do_execute()
    {
        state_reg.next[0] = {};
        state_reg.next[0].alu_result = alu_result_comb_func();
    }

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        do_execute();
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::work(clk, reset);
    }

    void strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::strobe();
    }
};
