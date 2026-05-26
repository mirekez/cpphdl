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
    _PORT(uint8_t)      write2_addr_in  = _ASSIGN( (uint8_t)0 );
    _PORT(bool)         write2_in       = _ASSIGN( false );
    _PORT(DTYPE)        write2_data_in  = _ASSIGN( (DTYPE)0 );

    _PORT(uint8_t)      read_addr0_in;
    _PORT(uint8_t)      read_addr1_in;
    _PORT(bool)         read_in          = _ASSIGN( false );
    _PORT(DTYPE)        read_data0_out   = _ASSIGN_COMB( data0_out_comb_func() );
    _PORT(DTYPE)        read_data1_out   = _ASSIGN_COMB( data1_out_comb_func() );
    _PORT(DTYPE)        reset_x10_in     = _ASSIGN( (DTYPE)0 );
    _PORT(DTYPE)        reset_x11_in     = _ASSIGN( (DTYPE)0 );
    _PORT(DTYPE)        x1_out           = _ASSIGN_COMB( x1_comb_func() );
    _PORT(DTYPE)        x10_out          = _ASSIGN_COMB( x10_comb_func() );
    _PORT(DTYPE)        x11_out          = _ASSIGN_COMB( x11_comb_func() );
    _PORT(DTYPE)        x16_out          = _ASSIGN_COMB( x16_comb_func() );
    _PORT(DTYPE)        x17_out          = _ASSIGN_COMB( x17_comb_func() );

    bool    debugen_in;

private:

    memory<u32,MEM_WIDTH/32,MEM_DEPTH> buffer;

    _LAZY_COMB(data0_out_comb, DTYPE)
        // Register files are normally write-first for same-cycle WB/decode.
        // Without this bypass a dependent instruction decoded while WB commits
        // can see the previous architectural value and lose high address bits.
        if (write_in() && write_addr_in() == read_addr0_in()) {
            data0_out_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == read_addr0_in()) {
            data0_out_comb = write2_data_in();
        }
        else {
            data0_out_comb = (DTYPE) buffer[read_addr0_in()];
        }
        return data0_out_comb;
    }

    _LAZY_COMB(data1_out_comb, DTYPE)
        if (write_in() && write_addr_in() == read_addr1_in()) {
            data1_out_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == read_addr1_in()) {
            data1_out_comb = write2_data_in();
        }
        else {
            data1_out_comb = (DTYPE) buffer[read_addr1_in()];
        }
        return data1_out_comb;
    }

    _LAZY_COMB(x1_comb, DTYPE)
        return x1_comb = (DTYPE)buffer[1];
    }

    _LAZY_COMB(x10_comb, DTYPE)
        if (write_in() && write_addr_in() == 10) {
            return x10_comb = write_data_in();
        }
        if (write2_in() && write2_addr_in() == 10) {
            return x10_comb = write2_data_in();
        }
        return x10_comb = (DTYPE)buffer[10];
    }

    _LAZY_COMB(x11_comb, DTYPE)
        if (write_in() && write_addr_in() == 11) {
            return x11_comb = write_data_in();
        }
        if (write2_in() && write2_addr_in() == 11) {
            return x11_comb = write2_data_in();
        }
        return x11_comb = (DTYPE)buffer[11];
    }

    _LAZY_COMB(x16_comb, DTYPE)
        if (write_in() && write_addr_in() == 16) {
            return x16_comb = write_data_in();
        }
        if (write2_in() && write2_addr_in() == 16) {
            return x16_comb = write2_data_in();
        }
        return x16_comb = (DTYPE)buffer[16];
    }

    _LAZY_COMB(x17_comb, DTYPE)
        if (write_in() && write_addr_in() == 17) {
            return x17_comb = write_data_in();
        }
        if (write2_in() && write2_addr_in() == 17) {
            return x17_comb = write2_data_in();
        }
        return x17_comb = (DTYPE)buffer[17];
    }

public:

    void _work(bool reset)
    {
        byte i;

        if (reset) {
            for (i=0; i < MEM_DEPTH; ++i) {
                buffer[i] = 0;
            }
            buffer[10] = reset_x10_in();
            buffer[11] = reset_x11_in();
        }

        if (debugen_in) {
            std::print("{:s}: port0: @{}({}){:08x}, port1: @{}({}){:08x} @{}({}){:08x}\n", __inst_name,
                write_addr_in(), (int)write_in(), write_data_in(),
                read_addr0_in(), (int)read_in(), read_data0_out(),
                read_addr1_in(), (int)read_in(), read_data1_out());
        }

        if (write_in()) {
#ifndef SYNTHESIS
            if (write_addr_in() == 1 && std::getenv("TRIBE_TRACE_REGFILE_RA") != nullptr) {
                std::print("trace-regfile-ra cycle={} value={:08x}\n", sys_clock, (uint32_t)write_data_in());
            }
#endif
            buffer[write_addr_in()] = write_data_in();
        }
        if (write2_in()) {
            buffer[write2_addr_in()] = write2_data_in();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        buffer.apply(checkpoint_fd);
    }

    void _assign() {}
};
/////////////////////////////////////////////////////////////////////////
