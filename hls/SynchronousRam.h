#pragma once
#include "cpphdl.h"
namespace cpphdl::hls {
// A registered 1R/1W bank. The controller owns validity and avoids collisions;
// RAM contents/data registers are intentionally not reset. Native C++ uses the
// same deferred-write/read-before-commit semantics. The small replacement keeps
// physical RAM ports recognizable even when the parent has many dynamic banks.
template<unsigned WORDS,unsigned DEPTH>
class [[clang::annotate(R"(CPPHDL_REPLACEMENT=
module cpphdl_hls_SynchronousRam #(parameter WORDS=1,parameter DEPTH=64)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 (* ramstyle="M20K", ram_style="block" *) reg [WORDS*32-1:0] words[0:DEPTH-1];
 always @(posedge clk) begin
   if(!reset) begin
     if(write_in)words[write_address_in]<=data_in;
     if(read_in)data_out<=words[read_address_in];
   end
 end
endmodule
;)")]] SynchronousRam:public cpphdl::Module {
public:
    using Data=cpphdl::array<WORDS,cpphdl::u32>;
    _PORT(bool) read_in;_PORT(bool) write_in;
    _PORT(uint32_t) read_address_in;_PORT(uint32_t) write_address_in;
    _PORT(Data) data_in;_PORT(Data) data_out;
private:
    cpphdl::memory<cpphdl::u32,WORDS,DEPTH> words;
    cpphdl::reg<Data> data{};
public:
    void _assign(){data_out=_ASSIGN_REG(data);}
    void _work(bool reset) {
        if(!reset) {
            if(write_in())words[write_address_in()]=data_in();
            if(read_in())data._next=words[read_address_in()];
        }
    }
    void _strobe(){words.apply();data.strobe();}
};
} // namespace cpphdl::hls
