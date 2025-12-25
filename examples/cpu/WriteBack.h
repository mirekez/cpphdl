#pragma once

using namespace cpphdl;


template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

    struct State
    {
    };

    reg<u32> regs_out_reg;
    reg<u8> regs_wr_id_reg;
    reg<u1> regs_write_reg;

public:
    __PORT(uint32_t)   mem_data_in;
    __PORT(uint32_t)   regs_data_out = __VAL( regs_out_reg );
    __PORT(uint8_t)    regs_wr_id_out = __VAL( regs_wr_id_reg );
    __PORT(bool)       regs_write_out = __VAL( regs_write_reg );

    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }

    void do_writeback()
    {
        // NOTE! reg0 is ZERO, never write it
        regs_wr_id_reg.next = state_in()[ID-1].rd;

        regs_write_reg.next = 0;
        switch (state_in()[ID-1].wb_op) {
            case Wb::ALU:
                regs_out_reg.next = state_in()[ID-1].alu_result;
                regs_write_reg.next = state_in()[ID-1].valid;
            break;
            case Wb::MEM:
                switch (state_in()[ID-1].funct3) {
                    case 0b000: regs_out_reg.next = int8_t(mem_data_in()); break;
                    case 0b001: regs_out_reg.next = int16_t(mem_data_in()); break;
                    case 0b010: regs_out_reg.next = int32_t(mem_data_in()); break;
                    case 0b100: regs_out_reg.next = uint8_t(mem_data_in()); break;
                    case 0b101: regs_out_reg.next = uint16_t(mem_data_in()); break;
                    default: regs_write_reg.next = 0; break;
                }
                regs_write_reg.next = state_in()[ID-1].valid;
            break;
        }
    }

    void work(bool clk, bool reset)
    {
        if (!clk) {
            return;
        }
        if (reset) {
            regs_write_reg.clr();
        }
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::work(clk, reset);  // first because it copies all registers from previous stage
        do_writeback();
    }

    void strobe()
    {
        PipelineStage<STATE,BIG_STATE,ID,LENGTH>::strobe();
        regs_out_reg.strobe();
        regs_wr_id_reg.strobe();
        regs_write_reg.strobe();
    }
};
