#pragma once
#include "cpphdl.h"

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH>
class File : public Module
{
    logic<MEM_WIDTH_BYTES*8> data0_out_comb;
    logic<MEM_WIDTH_BYTES*8> data1_out_comb;
    memory<u8,MEM_WIDTH_BYTES,MEM_DEPTH> buffer;

    size_t i;

public:
    uint8_t                   *write_addr_in   = nullptr;
    bool                      *write_in        = nullptr;
    logic<MEM_WIDTH_BYTES*8>  *write_data_in   = nullptr;

    uint8_t                   *read_addr0_in    = nullptr;
    uint8_t                   *read_addr1_in    = nullptr;
    bool                      *read_in          = nullptr;
    logic<MEM_WIDTH_BYTES*8>  *read_data0_out   = &data0_out_comb;
    logic<MEM_WIDTH_BYTES*8>  *read_data1_out   = &data1_out_comb;

    bool                      debugen_in;

    void connect() {}

    logic<MEM_WIDTH_BYTES*8>& data0_out_comb_func()
    {
        data0_out_comb = buffer[*read_addr0_in];
    }

    logic<MEM_WIDTH_BYTES*8>& data1_out_comb_func()
    {
        data1_out_comb = buffer[*read_addr1_in];
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;

        if (*write_in) {
            buffer[*write_addr_in] = *write_data_in;
        }
    }

    void strobe()
    {
        buffer.apply();
    }

    void comb() {}
};
/////////////////////////////////////////////////////////////////////////
