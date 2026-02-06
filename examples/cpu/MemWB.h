#pragma once

using namespace cpphdl;

extern unsigned long sys_clock;

template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class MemWB: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

private:
    STATIC uint32_t regs_out_comb;
    STATIC bool regs_write_comb;

    __LAZY_COMB(regs_out_comb, uint32_t&)

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

    __LAZY_COMB(regs_write_comb, bool&)

        regs_write_comb = 0;
        if (state_in()[ID-1].wb_op != Wb::WNONE) {
            regs_write_comb = state_in()[ID-1].valid;
        }
        return regs_write_comb;
    }

public:

    void _work(bool reset)
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_work(reset);  // first because it copies all registers from previous stage
    }

    void _strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::_strobe();
    }

    void _connect()
    {
//        std::print("MemWB: {} of {}\n", ID, LENGTH);
    }

    __PORT(uint32_t)   mem_data_in;
    __PORT(uint32_t)   regs_data_out = __VAR( regs_out_comb_func() );
    __PORT(uint8_t)    regs_wr_id_out = __EXPR( state_in()[ID-1].rd );  // NOTE! reg0 is ZERO, never write it
    __PORT(bool)       regs_write_out = __VAR( regs_write_comb_func() );

    struct State
    {
    };
};
