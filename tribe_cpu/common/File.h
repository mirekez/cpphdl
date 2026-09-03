#pragma once
#include "cpphdl.h"
#include "FileStorage.h"

extern long _system_clock;

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH, bool PRIMARY_WRITE_FIRST = true>
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

    FileStorage<MEM_WIDTH, MEM_DEPTH> storage;

    _LAZY_COMB(data0_out_comb, DTYPE)
        // Register files are normally write-first for same-cycle WB/decode.
        // Without this bypass a dependent instruction decoded while WB commits
        // can see the previous architectural value and lose high address bits.
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == read_addr0_in()) {
            data0_out_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == read_addr0_in()) {
            data0_out_comb = write2_data_in();
        }
        else {
            data0_out_comb = storage.read_data0_out();
        }
        return data0_out_comb;
    }

    _LAZY_COMB(data1_out_comb, DTYPE)
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == read_addr1_in()) {
            data1_out_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == read_addr1_in()) {
            data1_out_comb = write2_data_in();
        }
        else {
            data1_out_comb = storage.read_data1_out();
        }
        return data1_out_comb;
    }

    _LAZY_COMB(x1_comb, DTYPE)
        return x1_comb = storage.x1_out();
    }

    _LAZY_COMB(x10_comb, DTYPE)
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == 10) {
            x10_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == 10) {
            x10_comb = write2_data_in();
        }
        else {
            x10_comb = storage.x10_out();
        }
        return x10_comb;
    }

    _LAZY_COMB(x11_comb, DTYPE)
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == 11) {
            x11_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == 11) {
            x11_comb = write2_data_in();
        }
        else {
            x11_comb = storage.x11_out();
        }
        return x11_comb;
    }

    _LAZY_COMB(x16_comb, DTYPE)
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == 16) {
            x16_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == 16) {
            x16_comb = write2_data_in();
        }
        else {
            x16_comb = storage.x16_out();
        }
        return x16_comb;
    }

    _LAZY_COMB(x17_comb, DTYPE)
        if (PRIMARY_WRITE_FIRST && write_in() && write_addr_in() == 17) {
            x17_comb = write_data_in();
        }
        else if (write2_in() && write2_addr_in() == 17) {
            x17_comb = write2_data_in();
        }
        else {
            x17_comb = storage.x17_out();
        }
        return x17_comb;
    }

public:

    void _work(bool reset)
    {
        storage._work(reset);

        if (debugen_in) {
            std::print("{:s}: port0: @{}({}){:08x}, port1: @{}({}){:08x} @{}({}){:08x}\n", __inst_name,
                write_addr_in(), (int)write_in(), write_data_in(),
                read_addr0_in(), (int)read_in(), read_data0_out(),
                read_addr1_in(), (int)read_in(), read_data1_out());
        }

        if (write_in()) {
#ifndef SYNTHESIS
            if (write_addr_in() == 1 && std::getenv("TRIBE_TRACE_REGFILE_RA") != nullptr) {
                std::print("trace-regfile-ra cycle={} value={:08x}\n", _system_clock, (uint32_t)write_data_in());
            }
#endif
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        storage._strobe(checkpoint_fd);
    }

    void _assign()
    {
        storage.write_addr_in = write_addr_in;
        storage.write_in = write_in;
        storage.write_data_in = write_data_in;
        storage.write2_addr_in = write2_addr_in;
        storage.write2_in = write2_in;
        storage.write2_data_in = write2_data_in;
        storage.read_addr0_in = read_addr0_in;
        storage.read_addr1_in = read_addr1_in;
        storage.reset_x10_in = reset_x10_in;
        storage.reset_x11_in = reset_x11_in;
        storage.__inst_name = __inst_name + "/storage";
        storage._assign();
    }
};
/////////////////////////////////////////////////////////////////////////
