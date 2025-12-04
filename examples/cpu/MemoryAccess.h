#pragma once

using namespace cpphdl;

template<typename STATE, size_t ID, size_t LENGTH>
class MemoryAccess: public PipelineStage
{
public:
    struct State
    {
        uint32_t load_data = 0;   // valid only if Mem::LOAD
    };
    reg<array<State,LENGTH-ID>> state_reg;

    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8> mem_mask_reg;
    reg<u1> mem_write_reg;
    reg<u1> mem_read_reg;

public:
    array<STATE,LENGTH>       *state_in     = nullptr;
    array<State,LENGTH-ID>    *state_out    = &state_reg;
    uint32_t                  *mem_addr_out = &mem_addr_reg;
    uint32_t                  *mem_data_out = &mem_data_reg;
    uint8_t                   *mem_mask_out = &mem_mask_reg;
    bool                      *mem_write_out = &mem_write_reg;
    bool                      *mem_read_out = &mem_read_reg;

    void connect()
    {
        std::print("MemoryAccess: {} of {}\n", ID, LENGTH);
    }

    void do_memory()
    {
        state_reg.next[0] = {};

        mem_addr_reg.next = (*state_in)[ID-1].alu_result;
        mem_data_reg.next = (*state_in)[ID-1].rs2_val;


        if ((*state_in)[ID-1].mem_op == Mem::MNONE) {
            return;
        }

        mem_write_reg.next = 0;
        if ((*state_in)[ID-1].mem_op == Mem::STORE)  // parallel case, full case
        {
            switch ((*state_in)[ID-1].funct3)
            {
                case 0b000: // LB
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0x1;
                    break;
                case 0b001: // LH
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0x3;
                    break;
                case 0b010: // LW
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0xF;
                    break;
                default:
                    break;
            }
        }

        mem_read_reg.next = 0;
        if ((*state_in)[ID-1].mem_op == Mem::LOAD)
        {
            switch ((*state_in)[ID-1].funct3)
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
        do_memory();
    }

    void strobe()
    {
        state_reg.strobe();
        mem_addr_reg.strobe();
        mem_data_reg.strobe();
        mem_mask_reg.strobe();
        mem_write_reg.strobe();
        mem_read_reg.strobe();
    }
};
