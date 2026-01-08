#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t MEM_WIDTH_BYTES, size_t MEM_DEPTH, bool SHOWAHEAD = true>
class Memory : public Module
{
public:
    logic<MEM_WIDTH_BYTES*8> data0_out_comb;
    logic<MEM_WIDTH_BYTES*8> data1_out_comb;
    reg<logic<MEM_WIDTH_BYTES*8>> data0_out_reg;
    reg<logic<MEM_WIDTH_BYTES*8>> data1_out_reg;
    memory<u8,MEM_WIDTH_BYTES,MEM_DEPTH> buffer;

    size_t i;

public:
    __PORT(u<clog2(MEM_DEPTH)>)       addr0_in;
    __PORT(bool)                      write0_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  write0_data_in;
    __PORT(logic<MEM_WIDTH_BYTES>)    write0_mask_in;
    __PORT(bool)                      read0_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  read0_data_out = __VAL( read_data0_out_comb_func() );

    __PORT(u<clog2(MEM_DEPTH)>)       addr1_in;
    __PORT(bool)                      write1_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  write1_data_in;
    __PORT(logic<MEM_WIDTH_BYTES>)    write1_mask_in;
    __PORT(bool)                      read1_in;
    __PORT(logic<MEM_WIDTH_BYTES*8>)  read1_data_out = __VAL( read_data1_out_comb_func() );

    bool                      debugen_in;

    void connect() {}

    logic<MEM_WIDTH_BYTES*8>& read_data0_out_comb_func()
    {
        if (SHOWAHEAD) {
            data0_out_comb = buffer[addr0_in()];
        }
        else {
            data0_out_comb = data0_out_reg;
        }
        return data0_out_comb;
    }

    logic<MEM_WIDTH_BYTES*8>& read_data1_out_comb_func()
    {
        if (SHOWAHEAD) {
            data1_out_comb = buffer[addr1_in()];
        }
        else {
            data1_out_comb = data1_out_reg;
        }
        return data1_out_comb;
    }

    logic<MEM_WIDTH_BYTES*8> mask;

    void work(bool clk, bool reset)
    {
        if (!clk) return;
        if (write0_in()) {
            mask = 0;
            for (i=0; i < MEM_WIDTH_BYTES; ++i) {
                mask.bits((i+1)*8-1,i*8) = write0_mask_in()[i] ? 0xFF : 0 ;
            }
            buffer[addr0_in()] = (buffer[addr0_in()]&~mask) | (write0_data_in()&mask);
        }

        if (write1_in()) {
            mask = 0;
            for (i=0; i < MEM_WIDTH_BYTES; ++i) {
                mask.bits((i+1)*8-1,i*8) = write1_mask_in()[i] ? 0xFF : 0 ;
            }
            buffer[addr1_in()] = (buffer[addr1_in()]&~mask) | (write1_data_in()&mask);
        }

        if (!SHOWAHEAD) {
            data0_out_reg.next = buffer[addr0_in()];
            data1_out_reg.next = buffer[addr1_in()];
        }

        if (debugen_in) {
            std::print("{:s}: port0: @{}({}/{}){}({}){}, port1: @{}({}/{}){}({}){}\n", __inst_name,
                addr0_in(), (int)write0_in(), (int)read0_in(), write0_data_in(), write0_mask_in(), read0_data_out(),
                addr1_in(), (int)write1_in(), (int)read1_in(), write1_data_in(), write1_mask_in(), read1_data_out());
        }
    }

    void strobe()
    {
        buffer.apply();
        data0_out_reg.strobe();
        data1_out_reg.strobe();
    }

    void comb() {}
};
/////////////////////////////////////////////////////////////////////////
