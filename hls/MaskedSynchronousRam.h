#pragma once
#include "cpphdl.h"
namespace cpphdl::hls
{
// Word write enables keep gather/transpose in the memory interface. No
// read/modify/write transaction and no DRAM padding are required.
template <unsigned WORDS, unsigned DEPTH>
class [[clang::annotate(R"(CPPHDL_REPLACEMENT=
module cpphdl_hls_MaskedSynchronousRam #(parameter WORDS=16,parameter DEPTH=128)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS-1:0] write_mask_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 genvar lane;
 generate for(lane=0;lane<WORDS;lane=lane+1) begin: lanes
   (* ramstyle="M20K", ram_style="block" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane])words[write_address_in]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=words[read_address_in];
     end
   end
 end endgenerate
endmodule
;)")]] MaskedSynchronousRam : public cpphdl::Module
{
public:
    using Data = cpphdl::array<WORDS, cpphdl::u32>;
    _PORT(bool) read_in;
    _PORT(bool) write_in;
    _PORT(uint32_t) read_address_in;
    _PORT(uint32_t) write_address_in;
    _PORT(cpphdl::logic<WORDS>) write_mask_in;
    _PORT(Data) data_in;
    _PORT(Data) data_out;

private:
    cpphdl::memory<cpphdl::u32, 1, DEPTH> words[WORDS];
    cpphdl::reg<Data> data{};

public:
    void _assign()
    {
        data_out = _ASSIGN_REG(data);
    }
    void _work(bool reset)
    {
        if (!reset)
            for (uint32_t lane = 0; lane < WORDS; ++lane)
            {
                if (write_in() && bool(write_mask_in()[lane]))
                    words[lane][write_address_in()] =
                        cpphdl::array<1, cpphdl::u32>(data_in()[lane]);
                if (read_in())
                    data._next[lane] = words[lane][read_address_in()][0];
            }
    }
    void _strobe()
    {
        for (uint32_t lane = 0; lane < WORDS; ++lane)
            words[lane].apply();
        data.strobe();
    }
};
} // namespace cpphdl::hls
