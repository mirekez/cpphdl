#pragma once

using namespace cpphdl;

class ExecuteMem: public Module
{
public:
    __PORT(State)    state_in;
    __PORT(uint32_t) alu_result_in;

    __PORT(uint32_t) mem_size_out        = __VAR(mem_size_comb_func());
    __PORT(bool)     split_load_out      = __VAR(split_load_comb_func());
    __PORT(uint32_t) split_load_low_out  = __VAR(split_load_low_addr_comb_func());
    __PORT(uint32_t) split_load_high_out = __VAR(split_load_high_addr_comb_func());

private:
    __LAZY_COMB(mem_size_comb, uint32_t)
        mem_size_comb = 0;
        switch (state_in().funct3) {
            case 0b000: mem_size_comb = 1; break;
            case 0b001: mem_size_comb = 2; break;
            case 0b010: mem_size_comb = 4; break;
            case 0b100: mem_size_comb = 1; break;
            case 0b101: mem_size_comb = 2; break;
            default: break;
        }
        return mem_size_comb;
    }

    __LAZY_COMB(split_load_comb, bool)
        uint32_t size;
        size = mem_size_comb_func();
        split_load_comb = state_in().valid && state_in().wb_op == Wb::MEM &&
            size != 0 && ((alu_result_in() & 0x1f) + size > 32);
        return split_load_comb;
    }

    __LAZY_COMB(split_load_low_addr_comb, uint32_t)
        return split_load_low_addr_comb = alu_result_in() & ~3u;
    }

    __LAZY_COMB(split_load_high_addr_comb, uint32_t)
        return split_load_high_addr_comb = split_load_low_addr_comb_func() + 4;
    }

public:
    void _work(bool reset)
    {
    }

    void _strobe()
    {
    }

    void _assign()
    {
    }
};
