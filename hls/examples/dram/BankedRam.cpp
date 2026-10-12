// Direct clocked memory must emit RAM ports inside the clocked block, not a
// SystemVerilog task. Also cover reset aliases, early returns, keyword names.
#include "cpphdl.h"
class BankedRamTop:public cpphdl::Module {
    // (* ram_style = "block" *)
    cpphdl::memory<cpphdl::u32,1,256> words[1][2];
    cpphdl::reg<cpphdl::array<2,cpphdl::u32>> data;
    cpphdl::reg<cpphdl::u32> context;
    cpphdl::reg<cpphdl::u1> valid;
public:
    _PORT(bool) write_in;_PORT(bool) read_in;
    _PORT(uint32_t) write_address_in;_PORT(uint32_t) read_address_in;
    _PORT(cpphdl::array<2,cpphdl::u32>) write_data_in;_PORT(cpphdl::array<2,cpphdl::u32>) data_out;
    _PORT(bool) valid_out;_PORT(uint32_t) tag_out;
    void _assign(){data_out=_ASSIGN_REG(data);valid_out=_ASSIGN(bool(valid));tag_out=_ASSIGN(uint32_t(context));}
    void _work(bool clear_state) {
        if(clear_state){valid.clr();context.clr();data.clr();return;}
        valid._next=read_in();
        for(uint32_t bank=0;bank<2;++bank) {
            if(write_in())words[0][bank][write_address_in()]=uint32_t(write_data_in()[bank]);
            if(read_in())data._next[bank]=uint32_t(words[0][bank][read_address_in()]);
        }
        if(read_in())context._next=read_address_in();
    }
    void _strobe(){for(uint32_t bank=0;bank<2;++bank)words[0][bank].apply();data.strobe();context.strobe();valid.strobe();}
};
#ifndef SYNTHESIS
#include <cstdio>
#ifdef VERILATOR
#include "VBankedRamTop.h"
#ifdef SYNTH_GATES
#define reset work_reset
#define write_in write
#define read_in read
#define write_address_in write_address
#define read_address_in read_address
#define write_data_in write_data
#define data_out data
#define valid_out valid
#define tag_out tag
#endif
#endif
long _system_clock=0;
int main() {
#ifdef VERILATOR
    VBankedRamTop dut;
#else
    BankedRamTop dut;
#endif
    bool wr=false,rd=false;uint32_t wa=0,ra=0;cpphdl::array<2,cpphdl::u32> input;
#ifndef VERILATOR
    dut.write_in=_ASSIGN(wr);dut.read_in=_ASSIGN(rd);dut.write_address_in=_ASSIGN(wa);dut.read_address_in=_ASSIGN(ra);dut.write_data_in=_ASSIGN(input);dut._assign();
#endif
    auto step=[&](bool reset) {
#ifdef VERILATOR
        dut.clk=0;dut.reset=reset;dut.write_in=wr;dut.read_in=rd;dut.write_address_in=wa;dut.read_address_in=ra;dut.write_data_in=uint64_t(uint32_t(input[0]))|(uint64_t(uint32_t(input[1]))<<32);dut.eval();dut.clk=1;dut.eval();dut.clk=0;dut.eval();
#else
        dut._work(reset);dut._strobe();
#endif
        ++_system_clock;
    };
    step(true);wr=true;
    for(wa=0;wa<256;++wa){input[0]=wa*17+9;input[1]=wa*23+31;step(false);}
    wr=false;rd=true;
    for(ra=0;ra<256;++ra) {
        step(false);
#ifdef VERILATOR
        if(!dut.valid_out||dut.tag_out!=ra||uint32_t(dut.data_out)!=ra*17+9||uint32_t(dut.data_out>>32)!=ra*23+31)return 1;
#else
        if(!dut.valid_out()||dut.tag_out()!=ra||uint32_t(dut.data_out()[0])!=ra*17+9||uint32_t(dut.data_out()[1])!=ra*23+31)return 1;
#endif
    }
    step(true);
#ifdef VERILATOR
    if(dut.valid_out||dut.tag_out||dut.data_out)return 1;
#else
    if(dut.valid_out()||dut.tag_out()||uint32_t(dut.data_out()[0])||uint32_t(dut.data_out()[1]))return 1;
#endif
    std::puts("PASS inline clocked banked RAM: reset alias, early return, keyword register, 512 words");
}
#endif
