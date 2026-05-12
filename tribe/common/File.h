#pragma once
#include "cpphdl.h"

using namespace cpphdl;

extern long sys_clock;

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH>
class File : public Module
{
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32),uint32_t,uint64_t>;
public:

    _PORT(uint8_t)      write_addr_in;
    _PORT(bool)         write_in;
    _PORT(DTYPE)        write_data_in;

    _PORT(uint8_t)      read_addr0_in;
    _PORT(uint8_t)      read_addr1_in;
    _PORT(bool)         read_in          = _ASSIGN( false );
    _PORT(DTYPE)        read_data0_out   = _ASSIGN_REG( data0_out_comb_func() );
    _PORT(DTYPE)        read_data1_out   = _ASSIGN_REG( data1_out_comb_func() );

    bool    debugen_in;

private:

    memory<u32,MEM_WIDTH/32,MEM_DEPTH> buffer;

    _LAZY_COMB(data0_out_comb, DTYPE)
        return data0_out_comb = (DTYPE) buffer[read_addr0_in()];
    }

    _LAZY_COMB(data1_out_comb, DTYPE)
        return data1_out_comb = (DTYPE) buffer[read_addr1_in()];
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
            std::print("{:s}: port0: @{}({}){:08x}, port1: @{}({}){:08x} @{}({}){:08x}\n", __inst_name,
                write_addr_in(), (int)write_in(), write_data_in(),
                read_addr0_in(), (int)read_in(), read_data0_out(),
                read_addr1_in(), (int)read_in(), read_data1_out());
        }

        if (write_in()) {
            buffer[write_addr_in()] = write_data_in();
        }
    }

    void _strobe()
    {
        buffer.apply();
    }

    void _assign() {}
};
/////////////////////////////////////////////////////////////////////////
