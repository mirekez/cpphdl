#pragma once

using namespace cpphdl;

template<typename STATE, size_t ID, size_t LENGTH>
class ExecuteCalc: public PipelineStage
{
public:
    struct State
    {
        uint32_t alu_result;
    };
    reg<array<State,LENGTH-ID>> state_reg;

    bool     branch_taken_comb;
    uint32_t branch_target_comb;

public:
    array<STATE,LENGTH>       *state_in  = nullptr;
    array<State,LENGTH-ID>    *state_out = &state_reg;
    uint32_t                  *alu_result_out = (uint32_t*)&state_reg;
    bool                      *branch_taken_out = &branch_taken_comb;
    uint32_t                  *branch_target_out = &branch_target_comb;

    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }

    bool branch_taken_comb_func()
    {
        branch_taken_comb = false;
        switch ((*state_in)[ID-1].branch)
        {
            case Br::BEQ: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val == (*state_in)[ID-1].rs2_val); break;
            case Br::BNE: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val != (*state_in)[ID-1].rs2_val); break;
            case Br::BLT: state_reg.next[0].branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) < int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BGE: state_reg.next[0].branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) >= int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BLTU: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val < (*state_in)[ID-1].rs2_val); break;
            case Br::BGEU: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val >= (*state_in)[ID-1].rs2_val); break;
            case Br::JAL: state_reg.next[0].branch_taken_comb = true; break;
            case Br::JALR: state_reg.next[0].branch_taken_comb = true; break;
            case Br::BNONE: break;
        }
        return branch_taken_comb && (*state_in)[ID-1].valid;
    }

    bool branch_target_comb_func()
    {
        branch_target_comb = 0;
        if ((*state_in)[ID-1].branch != Br::BNONE)
        {
            if ((*state_in)[ID-1].branch == Br::JAL)
                state_reg.next[0].branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
            else if ((*state_in)[ID-1].branch == Br::JALR)
                state_reg.next[0].branch_target_comb = ((*state_in)[ID-1].rs1_val + (*state_in)[ID-1].imm) & ~1u;
            else     // conditional branch
                state_reg.next[0].branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
        }
        return branch_target_comb;
    }

    void do_execute()
    {
        state_reg.next[0] = {};

        uint32_t a = (*state_in)[ID-1].rs1_val;
        uint32_t b = ((*state_in)[ID-1].alu_op == Alu::ADD && (*state_in)[ID-1].mem_op != Mem::MNONE)
                        ? uint32_t((*state_in)[ID-1].imm)      // load/store address calc uses imm
                        : (*state_in)[ID-1].rs2 ? (*state_in)[ID-1].rs2_val : uint32_t((*state_in)[ID-1].imm);

        switch ((*state_in)[ID-1].alu_op)
        {
            case Alu::ADD:  state_reg.next[0].alu_result = a + b; break;
            case Alu::SUB:  state_reg.next[0].alu_result = a - b; break;
            case Alu::AND:  state_reg.next[0].alu_result = a & b; break;
            case Alu::OR:   state_reg.next[0].alu_result = a | b; break;
            case Alu::XOR:  state_reg.next[0].alu_result = a ^ b; break;
            case Alu::SLL:  state_reg.next[0].alu_result = a << (b & 0x1F); break;
            case Alu::SRL:  state_reg.next[0].alu_result = a >> (b & 0x1F); break;
            case Alu::SRA:  state_reg.next[0].alu_result = uint32_t(int32_t(a) >> (b & 0x1F)); break;
            case Alu::SLT:  state_reg.next[0].alu_result = (int32_t(a) < int32_t(b)); break;
            case Alu::SLTU: state_reg.next[0].alu_result = (a < b); break;
            case Alu::PASS_RS1: state_reg.next[0].alu_result = a; break;
            case Alu::ANONE: break;
        }


    }

    void work(bool clk, bool reset)
    {
        do_execute();
    }

    void strobe()
    {
        state_reg.strobe();
    }
};
