#pragma once

using namespace cpphdl;


template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class MemWB: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

    struct State
    {
    };

    uint32_t regs_out_comb;
    bool regs_write_comb;

public:
    __PORT(uint32_t)   mem_data_in;
    __PORT(uint32_t)   regs_data_out = __VAL( regs_out_comb_func() );
    __PORT(uint8_t)    regs_wr_id_out = __VAL( state_in()[ID-1].rd );  // NOTE! reg0 is ZERO, never write it
    __PORT(bool)       regs_write_out = __VAL( regs_write_comb_func() );

    void _connect()
    {
        std::print("MemWB: {} of {}\n", ID, LENGTH);
    }

    uint32_t regs_out_comb_func()
    {
        regs_out_comb = 0;
        if (state_in()[ID-1].wb_op == Wb::PC2) {
            regs_out_comb = state_in()[ID-1].pc + 2;
        }
        else if (state_in()[ID-1].wb_op == Wb::PC4) {
            regs_out_comb = state_in()[ID-1].pc + 4;
        }
        else if (state_in()[ID-1].wb_op == Wb::ALU) {
            regs_out_comb = state_in()[ID-1].alu_result;
        }
        else if (state_in()[ID-1].wb_op == Wb::MEM) {
            switch (state_in()[ID-1].funct3) {
                case 0b000: regs_out_comb = int8_t(mem_data_in()); break;
                case 0b001: regs_out_comb = int16_t(mem_data_in()); break;
                case 0b010: regs_out_comb = int32_t(mem_data_in()); break;
                case 0b100: regs_out_comb = uint8_t(mem_data_in()); break;
                case 0b101: regs_out_comb = uint16_t(mem_data_in()); break;
            }
        }
        return regs_out_comb;
    }

    bool regs_write_comb_func()
    {
        regs_write_comb = 0;
        if (state_in()[ID-1].wb_op != Wb::WNONE) {
            regs_write_comb = state_in()[ID-1].valid;
        }
        return regs_write_comb;
    }

    void _work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        if (reset) {
        }
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_work(clk, reset);  // first because it copies all registers from previous stage
    }

    void _strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_strobe();
    }
};
