#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "cpphdl.h"
namespace cpphdl_examples {
// Word write enables keep gather/transpose in the memory interface. No
// read/modify/write transaction and no DRAM padding are required.
template <unsigned WORDS, unsigned DEPTH>
class [[clang::annotate(R"(CPPHDL_REPLACEMENT=
module cpphdl_examples_MaskedSynchronousRam #(parameter WORDS=16,parameter DEPTH=128)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS-1:0] write_mask_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 genvar lane;
 generate for(lane=0;lane<WORDS;lane=lane+1) begin: lanes
 if(DEPTH<=64) begin: shallow
   (* ramstyle="MLAB", ram_style="distributed" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane])words[write_address_in]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=words[read_address_in];
     end
   end
 end else begin: deep
   (* ramstyle="M20K", ram_style="block" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane])words[write_address_in]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=words[read_address_in];
     end
   end
 end
 end endgenerate
endmodule
;)")]] MaskedSynchronousRam : public cpphdl::Module {
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
    void _assign() { data_out = _ASSIGN_REG(data); }
    void _work(bool reset) {
        if (!reset)
            for (uint32_t lane = 0; lane < WORDS; ++lane) {
                if (write_in() && bool(write_mask_in()[lane]))
                    words[lane][write_address_in()] =
                        cpphdl::array<1, cpphdl::u32>(data_in()[lane]);
                if (read_in())
                    data._next[lane] = words[lane][read_address_in()][0];
            }
    }
    void _strobe() {
        for (uint32_t lane = 0; lane < WORDS; ++lane)
            words[lane].apply();
        data.strobe();
    }
};
} // namespace cpphdl_examples
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
class MaskedSynchronousRamExample : public cpphdl::Module {
    cpphdl_examples::MaskedSynchronousRam<3, 17> ram;

  public:
    using Data = cpphdl::array<3, cpphdl::u32>;
    _PORT(bool) read_in, write_in;
    _PORT(uint32_t) read_address_in, write_address_in;
    _PORT(cpphdl::logic<3>) write_mask_in;
    _PORT(Data) data_in, data_out;
    void _assign() {
        ram.read_in = read_in;
        ram.write_in = write_in;
        ram.read_address_in = read_address_in;
        ram.write_address_in = write_address_in;
        ram.write_mask_in = write_mask_in;
        ram.data_in = data_in;
        data_out = _ASSIGN(ram.data_out());
        ram._assign();
    }
    void _work(bool reset) { ram._work(reset); }
    void _strobe() { ram._strobe(); }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <array>
#include <cstdio>
#include <stdexcept>
#ifdef VERILATOR
#include "VMaskedSynchronousRamExample.h"
#endif
long _system_clock = 0;

int main_from_header(int, char**) {
#ifdef VERILATOR
    VMaskedSynchronousRamExample dut;
#else
    MaskedSynchronousRamExample dut;
#endif
    bool read = false, write = false;
    uint32_t ra = 0, wa = 0;
    cpphdl::logic<3> mask = 0;
    MaskedSynchronousRamExample::Data input{};
    std::array<std::array<uint32_t, 3>, 17> memory{};
    std::array<uint32_t, 3> expected{};
    bool known = false;
#ifndef VERILATOR
    dut.read_in = _ASSIGN(read);
    dut.write_in = _ASSIGN(write);
    dut.read_address_in = _ASSIGN(ra);
    dut.write_address_in = _ASSIGN(wa);
    dut.write_mask_in = _ASSIGN(mask);
    dut.data_in = _ASSIGN(input);
    dut._assign();
#endif
    auto cycle = [&](bool reset) {
        // The reference reads before writing, including address collisions.
        if (!reset && read) {
            expected = memory[ra];
            known = true;
        }
        if (!reset && write)
            for (unsigned lane = 0; lane < 3; ++lane)
                if (bool(mask[lane]))
                    memory[wa][lane] = uint32_t(input[lane]);
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.read_in = read;
        dut.write_in = write;
        dut.read_address_in = ra;
        dut.write_address_in = wa;
        dut.write_mask_in = uint32_t(mask);
        for (unsigned lane = 0; lane < 3; ++lane)
            dut.data_in[lane] = uint32_t(input[lane]);
        dut.eval();
        dut.clk = 1;
        dut.eval();
        dut.clk = 0;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
        if (known)
            for (unsigned lane = 0; lane < 3; ++lane) {
#ifdef VERILATOR
                uint32_t actual = dut.data_out[lane];
#else
                uint32_t actual = dut.data_out()[lane];
#endif
                if (actual != expected[lane]) {
                    std::fprintf(stderr, "cycle %ld lane %u: %08x != %08x\n", _system_clock, lane,
                                 actual, expected[lane]);
                    throw std::runtime_error("masked RAM mismatch");
                }
            }
    };
    cycle(true);
    write = true;
    mask = 7;
    for (wa = 0; wa < 17; ++wa) {
        for (unsigned lane = 0; lane < 3; ++lane)
            input[lane] = wa * 31 + lane;
        cycle(false);
    }
    uint32_t random = 0x13572468;
    auto next = [&]() {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    for (unsigned i = 0; i < 20000; ++i) {
        wa = next() % 17;
        ra = i % 2 ? wa : next() % 17;
        read = i % 5 != 0;
        write = i % 7 != 0;
        mask = i % 8;
        for (unsigned lane = 0; lane < 3; ++lane)
            input[lane] = next();
        cycle(i % 97 == 0);
    }
    write = false;
    read = true;
    for (ra = 0; ra < 17; ++ra)
        cycle(false);
    std::puts("Masked RAM: all masks, read-first collisions, hold and reset retention passed");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
