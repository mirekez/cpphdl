#pragma once
#include "cpphdl.h"
#include "Memory.h"

using namespace cpphdl;

extern long sys_clock;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH, int ID = 0>
class Ram : public Module
{
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32),uint32_t,uint64_t>;
public:

    __PORT(DTYPE)        write_addr_in;
    __PORT(bool)         write_in;
    __PORT(DTYPE)        write_data_in;
    __PORT(uint8_t)      write_mask_in;

    __PORT(DTYPE)        read_addr_in;
    __PORT(bool)         read_in;
    __PORT(DTYPE)        read_data_out   = __EXPR( ((DTYPE)ram.read0_data_out() >> (read_addr_in()%4*8)) |
                     ( read_addr_in()%4 == 0 ? 0 : ((DTYPE)ram.read1_data_out() << (32-read_addr_in()%4*8)) ) );  // combine 2 words on read

    bool    debugen_in;


    Memory<MEM_WIDTH/8,MEM_DEPTH,true,ID> ram;

public:

    void _work(bool reset)
    {
        ram._work(reset);
    }

    void _strobe()
    {
        ram._strobe();
    }

    void _connect()
    {
        ram.addr0_in = __EXPR((u<clog2(MEM_DEPTH)>) (write_in() ? write_addr_in()/4 : read_addr_in()/4) );
        ram.addr1_in = __EXPR((u<clog2(MEM_DEPTH)>) (write_in() ? ( write_addr_in()%4 ? write_addr_in()/4+1 : 0 ) : ( read_addr_in()%4 ? read_addr_in()/4+1 : 0 )) );
        ram.write0_in = write_in;
        ram.write1_in = __EXPR( write_addr_in()%4*8 == 0 ? false : write_in() );
        ram.write0_data_in = __EXPR( cpphdl::logic<MEM_WIDTH>(write_data_in() << (write_addr_in()%4*8)) );  // combine word from two
        ram.write1_data_in = __EXPR( cpphdl::logic<MEM_WIDTH>(write_addr_in()%4 == 0 ? 0 : write_data_in() >> (32-write_addr_in()%4*8)) );
        ram.write0_mask_in = __EXPR( logic<MEM_WIDTH/8>(write_mask_in() << (write_addr_in()%4)) );  // combine mask from two
        ram.write1_mask_in = __EXPR( logic<MEM_WIDTH/8>(write_mask_in() >> (4-write_addr_in()%4)) );
        ram.read0_in = read_in;
        ram.read1_in = __EXPR( read_addr_in()%4 == 0 ? false : read_in() );  // when we need to read 2 words
        ram.__inst_name = __inst_name + "/ram";
        ram.debugen_in  = debugen_in;
        ram._connect();
    }
};
/////////////////////////////////////////////////////////////////////////
