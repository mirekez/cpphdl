#pragma once
#include "cpphdl.h"

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH>
class File : public Module
{
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32),uint32_t,uint64_t>;
    DTYPE data0_out_comb;
    DTYPE data1_out_comb;
    memory<u32,MEM_WIDTH/32,MEM_DEPTH> buffer;

    size_t i;

public:
    __PORT(uint8_t)      write_addr_in;
    __PORT(bool)         write_in;
    __PORT(DTYPE)        write_data_in;

    __PORT(uint8_t)      read_addr0_in;
    __PORT(uint8_t)      read_addr1_in;
    __PORT(bool)         read_in;
    __PORT(DTYPE)        read_data0_out   = __VAL( data0_out_comb_func() );
    __PORT(DTYPE)        read_data1_out   = __VAL( data1_out_comb_func() );

    bool    debugen_in;

    void _connect() {}

    DTYPE data0_out_comb_func()
    {
        return data0_out_comb = (logic<MEM_WIDTH>) buffer[read_addr0_in()];
    }

    DTYPE data1_out_comb_func()
    {
        return data1_out_comb = (logic<MEM_WIDTH>) buffer[read_addr1_in()];
    }

    void _work(bool clk, bool reset)
    {
        if (!clk) return;

        if (write_in()) {
            buffer[write_addr_in()] = write_data_in();
        }
    }

    void _strobe()
    {
        buffer.apply();
    }
};
/////////////////////////////////////////////////////////////////////////
