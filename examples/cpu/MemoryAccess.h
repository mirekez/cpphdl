#pragma once

using namespace cpphdl;


template<typename STATE, typename BIG_STATE, size_t ID, size_t LENGTH>
class MemoryAccess: public PipelineStage<STATE,BIG_STATE,ID,LENGTH>
{
public:
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_reg;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_in;
    using PipelineStage<STATE,BIG_STATE,ID,LENGTH>::state_out;

    struct State
    {
        uint32_t load_data = 0;   // valid only if Mem::LOAD
    };

    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8> mem_mask_reg;
    reg<u1> mem_write_reg;
    reg<u1> mem_read_reg;

public:
    __PORT(bool)        mem_write_out      = __VAL( mem_write_reg );
    __PORT(uint32_t)    mem_write_addr_out = __VAL( mem_addr_reg );
    __PORT(uint32_t)    mem_write_data_out = __VAL( mem_data_reg );
    __PORT(uint8_t)     mem_write_mask_out = __VAL( mem_mask_reg );
    __PORT(bool)        mem_read_out       = __VAL( mem_read_reg );
    __PORT(uint32_t)    mem_read_addr_out  = __VAL( mem_addr_reg );

    void connect()
    {
        std::print("MemoryAccess: {} of {}\n", ID, LENGTH);
    }

    void do_memory()
    {
        state_reg.next[0] = {};

        mem_addr_reg.next = state_in()[ID-1].alu_result;
        mem_data_reg.next = state_in()[ID-1].rs2_val;


        if (state_in()[ID-1].mem_op == Mem::MNONE) {
            return;
        }

        mem_write_reg.next = 0;
        if (state_in()[ID-1].mem_op == Mem::STORE)  // parallel case, full case
        {
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
                default:
                    break;
            }
        }

        mem_read_reg.next = 0;
        if (state_in()[ID-1].mem_op == Mem::LOAD)
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
        do_memory();
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
