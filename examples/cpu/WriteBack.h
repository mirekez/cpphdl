#pragma once

using namespace cpphdl;

template<typename STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage
{
public:
    struct State
    {
    };
    reg<array<State,LENGTH-ID>> state_reg;

    reg<u32> regs_out_reg;
    reg<u8> regs_wr_id_reg;
    reg<u1> regs_write_reg;

public:
    array<STATE,LENGTH>       *state_in      = nullptr;
    array<State,LENGTH-ID>    *state_out     = &state_reg;
    uint32_t                  *mem_data_in   = nullptr;
    uint32_t                  *regs_data_out = &regs_out_reg;
    uint8_t                   *regs_wr_id_out = &regs_wr_id_reg;
    bool                      *regs_write_out = &regs_write_reg;

    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }

    void do_writeback()
    {
        // NOTE! reg0 is ZERO, never write it
        regs_wr_id_reg.next = (*state_in)[ID-1].rd;

        regs_write_reg.next = 0;
        switch ((*state_in)[ID-1].wb_op) {
            case Wb::ALU:
                regs_out_reg.next = (*state_in)[ID-1].alu_result;
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
            case Wb::MEMORY:
                switch ((*state_in)[ID-1].funct3) {
                    case 0b000: regs_out_reg.next = int8_t(*mem_data_in); break;
                    case 0b001: regs_out_reg.next = int16_t(*mem_data_in); break;
                    case 0b010: regs_out_reg.next = int32_t(*mem_data_in); break;
                    case 0b100: regs_out_reg.next = uint8_t(*mem_data_in); break;
                    case 0b101: regs_out_reg.next = uint16_t(*mem_data_in); break;
                    default: regs_write_reg.next = 0; break;
                }
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
        }
    }

    void work(bool clk, bool reset)
    {
        do_writeback();
    }

    void strobe()
    {
        state_reg.strobe();
        regs_out_reg.strobe();
        regs_wr_id_reg.strobe();
        regs_write_reg.strobe();
    }
};
