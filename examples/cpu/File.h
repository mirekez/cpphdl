#pragma once
#include "cpphdl.h"

using namespace cpphdl;

extern unsigned long sys_clock;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH>
class File : public Module
{
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32),uint32_t,uint64_t>;
public:

    __PORT(uint8_t)      write_addr_in;
    __PORT(bool)         write_in;
    __PORT(DTYPE)        write_data_in;

    __PORT(uint8_t)      read_addr0_in;
    __PORT(uint8_t)      read_addr1_in;
    __PORT(bool)         read_in          = __EXPR( false );
    __PORT(DTYPE)        read_data0_out   = __VAR( data0_out_comb_func() );
    __PORT(DTYPE)        read_data1_out   = __VAR( data1_out_comb_func() );

    bool    debugen_in;

private:

    memory<u32,MEM_WIDTH/32,MEM_DEPTH> buffer;

    __LAZY_COMB(data0_out_comb, DTYPE)
        return data0_out_comb = (logic<MEM_WIDTH>) buffer[read_addr0_in()];
    }

    __LAZY_COMB(data1_out_comb, DTYPE)
        return data1_out_comb = (logic<MEM_WIDTH>) buffer[read_addr1_in()];
    }

public:

    void _work(bool reset)
    {
        byte i;

        if (reset) {
            for (i=0; i < MEM_DEPTH; ++i) {
                buffer[i] = 0;
            }
        }

        if (debugen_in) {
            std::print("{:s}: port0: @{}({}){}, port1: @{}({}){} @{}({}){}\n", __inst_name,
                write_addr_in(), write_in(), write_data_in(),
                read_addr0_in(), read_in(), read_data0_out(),
                read_addr1_in(), read_in(), read_data1_out());
        }

        if (write_in()) {
            buffer[write_addr_in()] = write_data_in();
        }
    }

    void _strobe()
    {
        buffer.apply();
    }


    void _connect() {}
};
/////////////////////////////////////////////////////////////////////////
