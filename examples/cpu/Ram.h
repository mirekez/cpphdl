#pragma once
#include "cpphdl.h"
#include "Memory.h"

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH, size_t MEM_DEPTH>
class Ram : public Module
{
public:
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32),uint32_t,uint64_t>;
    Memory<MEM_WIDTH/8,MEM_DEPTH,true> ram;

    size_t i;

public:
    __PORT(DTYPE)        write_addr_in;
    __PORT(bool)         write_in;
    __PORT(DTYPE)        write_data_in;
    __PORT(DTYPE)        write_mask_in;

    __PORT(DTYPE)        read_addr_in;
    __PORT(bool)         read_in;
    __PORT(DTYPE)        read_data_out   = __VAL( ((DTYPE)ram.read0_data_out() >> (read_addr_in()%4*8)) |
                 ( read_addr_in()%4 == 0 ? 0 : ((DTYPE)ram.read1_data_out() << (32-read_addr_in()%4*8)) ) );

    bool    debugen_in;

    void _connect()
    {
        ram.addr0_in = __VAL( write_in() ? write_addr_in()/4 : read_addr_in()/4 );
        ram.addr1_in = __VAL( write_in() ? ( write_addr_in()%4 ? write_addr_in()/4+1 : 0 ) : ( read_addr_in()%4 ? read_addr_in()/4+1 : 0 ) );
        ram.write0_in = write_in;
        ram.write1_in = __VAL( write_addr_in()%4*8 == 0 ? false : write_in() );
        ram.write0_data_in = __VAL( write_data_in() << (write_addr_in()%4*8) );
        ram.write1_data_in = __VAL( write_addr_in()%4 == 0 ? 0 : write_data_in() >> (32-write_addr_in()%4*8) );
        ram.write0_mask_in = __VAL( write_mask_in() << (write_addr_in()%4) );
        ram.write1_mask_in = __VAL( write_mask_in() >> (4-write_addr_in()%4) );
        ram.read0_in = read_in;
        ram.read1_in = __VAL( read_addr_in()%4 == 0 ? false : read_in() );
        ram.__inst_name = __inst_name + "/ram";
        ram.debugen_in  = debugen_in;
        ram._connect();
    }

    void _work(bool clk, bool reset)
    {
        if (!clk) return;

        ram._work(clk, reset);
    }

    void _strobe()
    {
        ram._strobe();
    }
};
/////////////////////////////////////////////////////////////////////////
