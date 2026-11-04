#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "cpphdl.h"
namespace cpphdl_examples {
// A registered 1R/1W bank. The controller owns validity and avoids collisions;
// RAM contents/data registers are intentionally not reset. Native C++ uses the
// same deferred-write/read-before-commit semantics. The small replacement keeps
// physical RAM ports recognizable even when the parent has many dynamic banks.
template <unsigned WORDS, unsigned DEPTH>
class [[clang::annotate(R"(CPPHDL_REPLACEMENT=
module cpphdl_examples_SynchronousRam #(parameter WORDS=1,parameter DEPTH=64)(
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
;)")]] SynchronousRam : public cpphdl::Module {
  public:
    using Data = cpphdl::array<WORDS, cpphdl::u32>;
    _PORT(bool) read_in;
    _PORT(bool) write_in;
    _PORT(uint32_t) read_address_in;
    _PORT(uint32_t) write_address_in;
    _PORT(Data) data_in;
    _PORT(Data) data_out;

  private:
    cpphdl::memory<cpphdl::u32, WORDS, DEPTH> words;
    cpphdl::reg<Data> data{};

  public:
    void _assign() { data_out = _ASSIGN_REG(data); }
    void _work(bool reset) {
        if (!reset) {
            if (write_in())
                words[write_address_in()] = data_in();
            if (read_in())
                data._next = words[read_address_in()];
        }
    }
    void _strobe() {
        words.apply();
        data.strobe();
    }
};
} // namespace cpphdl_examples
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
class SynchronousRamExample : public cpphdl::Module {
    cpphdl_examples::SynchronousRam<1, 64> narrow;
    cpphdl_examples::SynchronousRam<3, 64> wide;

  public:
    using Data = cpphdl::array<3, cpphdl::u32>;
    _PORT(bool) write_in, read_in;
    _PORT(uint32_t) write_address_in, read_address_in;
    _PORT(Data) write_data_in, data_out;
    _PORT(uint32_t) narrow_out;
    void _assign() {
        narrow.write_in = _ASSIGN(write_in());
        wide.write_in = _ASSIGN(write_in());
        narrow.read_in = _ASSIGN(read_in());
        wide.read_in = _ASSIGN(read_in());
        narrow.write_address_in = _ASSIGN(write_address_in());
        wide.write_address_in = _ASSIGN(write_address_in());
        narrow.read_address_in = _ASSIGN(read_address_in());
        wide.read_address_in = _ASSIGN(read_address_in());
        narrow.data_in = _ASSIGN(cpphdl::array<1, cpphdl::u32>{write_data_in()[0]});
        wide.data_in = _ASSIGN(write_data_in());
        data_out = _ASSIGN(wide.data_out());
        narrow_out = _ASSIGN(uint32_t(narrow.data_out()[0]));
        narrow._assign();
        wide._assign();
    }
    void _work(bool reset) {
        narrow._work(reset);
        wide._work(reset);
    }
    void _strobe() {
        narrow._strobe();
        wide._strobe();
    }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cstdio>
#ifdef VERILATOR
#include "VSynchronousRamExample.h"
#ifdef SYNTH_GATES
#define reset work_reset
#define write_in write
#define read_in read
#define write_address_in write_address
#define read_address_in read_address
#define write_data_in write_data
#define data_out data
#define narrow_out narrow
#endif
#endif
long _system_clock = 0;
int main_from_header(int, char**) {
#ifdef VERILATOR
    VSynchronousRamExample dut;
#else
    SynchronousRamExample dut;
#endif
    bool write = false, read = false;
    uint32_t write_address = 0, read_address = 0;
    SynchronousRamExample::Data input;
#ifndef VERILATOR
    dut.write_in = _ASSIGN(write);
    dut.read_in = _ASSIGN(read);
    dut.write_address_in = _ASSIGN(write_address);
    dut.read_address_in = _ASSIGN(read_address);
    dut.write_data_in = _ASSIGN(input);
    dut._assign();
#endif
    auto cycle = [&](bool reset) {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.write_in = write;
        dut.read_in = read;
        dut.write_address_in = write_address;
        dut.read_address_in = read_address;
        for (unsigned lane = 0; lane < 3; ++lane)
            dut.write_data_in[lane] = uint32_t(input[lane]);
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
    };
    auto output = [&](unsigned lane) -> uint32_t {
#ifdef VERILATOR
        return dut.data_out[lane];
#else
        return dut.data_out()[lane];
#endif
    };
    cycle(true);
    write = true;
    for (write_address = 0; write_address < 64; ++write_address) {
        for (unsigned lane = 0; lane < 3; ++lane)
            input[lane] = write_address * 17 + lane * 31 + 9;
        cycle(false);
    }
    write = false;
    read = true;
    for (read_address = 0; read_address < 64; ++read_address) {
        cycle(false);
        for (unsigned lane = 0; lane < 3; ++lane)
            if (output(lane) != read_address * 17 + lane * 31 + 9)
                return 1;
#ifdef VERILATOR
        if (dut.narrow_out != output(0))
            return 2;
#else
        if (dut.narrow_out() != output(0))
            return 2;
#endif
    }
    read_address = 0;
    cycle(true);
    if (output(0) != 63 * 17 + 9)
        return 3;
    cycle(false);
    if (output(0) != 9)
        return 4;
    read = false;
    read_address = 27;
    cycle(false);
    if (output(0) != 9)
        return 5;
    read = true;
    write = true;
    write_address = 32;
    read_address = 31;
    for (unsigned lane = 0; lane < 3; ++lane)
        input[lane] = 0xaabb0000u + lane;
    cycle(false);
    if (output(0) != 31 * 17 + 9)
        return 6;
    write = false;
    read_address = 32;
    cycle(false);
    for (unsigned lane = 0; lane < 3; ++lane)
        if (output(lane) != 0xaabb0000u + lane)
            return 7;
    write = true;
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        for (unsigned lane = 0; lane < 3; ++lane)
            input[lane] = iteration * 17 + lane;
        cycle(false);
        for (unsigned lane = 0; lane < 3; ++lane) {
            uint32_t old = iteration ? (iteration - 1) * 17 + lane : 0xaabb0000u + lane;
            if (output(lane) != old)
                return 8;
        }
    }
    std::puts("Synchronous RAM: 32/96-bit banks, registered reads, hold, reset retention and "
              "read-first collisions passed");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
