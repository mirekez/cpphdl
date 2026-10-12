#include "../SynchronousRam.h"

class RamPrimitiveTop : public cpphdl::Module {
    cpphdl::hls::SynchronousRam<1, 64> narrow;
    cpphdl::hls::SynchronousRam<3, 64> wide;
public:
    using Data = cpphdl::array<3, cpphdl::u32>;
    _PORT(bool) write_in, read_in;
    _PORT(uint32_t) write_address_in, read_address_in;
    _PORT(Data) write_data_in, data_out;
    _PORT(uint32_t) narrow_out;
    void _assign() {
        narrow.write_in = _ASSIGN(write_in()); wide.write_in = _ASSIGN(write_in());
        narrow.read_in = _ASSIGN(read_in()); wide.read_in = _ASSIGN(read_in());
        narrow.write_address_in = _ASSIGN(write_address_in()); wide.write_address_in = _ASSIGN(write_address_in());
        narrow.read_address_in = _ASSIGN(read_address_in()); wide.read_address_in = _ASSIGN(read_address_in());
        narrow.data_in = _ASSIGN(cpphdl::array<1, cpphdl::u32>{write_data_in()[0]});
        wide.data_in = _ASSIGN(write_data_in());
        data_out = _ASSIGN(wide.data_out()); narrow_out = _ASSIGN(uint32_t(narrow.data_out()[0]));
        narrow._assign(); wide._assign();
    }
    void _work(bool reset) { narrow._work(reset); wide._work(reset); }
    void _strobe() { narrow._strobe(); wide._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
#ifdef VERILATOR
#include "VRamPrimitiveTop.h"
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
int main() {
#ifdef VERILATOR
    VRamPrimitiveTop dut;
#else
    RamPrimitiveTop dut;
#endif
    bool write = false, read = false;
    uint32_t write_address = 0, read_address = 0;
    RamPrimitiveTop::Data input;
#ifndef VERILATOR
    dut.write_in = _ASSIGN(write); dut.read_in = _ASSIGN(read);
    dut.write_address_in = _ASSIGN(write_address); dut.read_address_in = _ASSIGN(read_address);
    dut.write_data_in = _ASSIGN(input); dut._assign();
#endif
    auto cycle = [&](bool reset) {
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset; dut.write_in = write; dut.read_in = read;
        dut.write_address_in = write_address; dut.read_address_in = read_address;
        for (unsigned lane = 0; lane < 3; ++lane) dut.write_data_in[lane] = uint32_t(input[lane]);
        dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#else
        dut._work(reset); dut._strobe();
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
    cycle(true); write = true;
    for (write_address = 0; write_address < 64; ++write_address) {
        for (unsigned lane = 0; lane < 3; ++lane) input[lane] = write_address * 17 + lane * 31 + 9;
        cycle(false);
    }
    write = false; read = true;
    for (read_address = 0; read_address < 64; ++read_address) {
        cycle(false);
        for (unsigned lane = 0; lane < 3; ++lane)
            if (output(lane) != read_address * 17 + lane * 31 + 9) return 1;
#ifdef VERILATOR
        if (dut.narrow_out != output(0)) return 2;
#else
        if (dut.narrow_out() != output(0)) return 2;
#endif
    }
    read_address = 0; cycle(true);
    if (output(0) != 63 * 17 + 9) return 3;
    cycle(false);
    if (output(0) != 9) return 4;
    read = false; read_address = 27; cycle(false);
    if (output(0) != 9) return 5;
    read = true; write = true; write_address = 32; read_address = 31;
    for (unsigned lane = 0; lane < 3; ++lane) input[lane] = 0xaabb0000u + lane;
    cycle(false);
    if (output(0) != 31 * 17 + 9) return 6;
    write = false; read_address = 32; cycle(false);
    for (unsigned lane = 0; lane < 3; ++lane) if (output(lane) != 0xaabb0000u + lane) return 7;
    std::puts("Synchronous RAM: 32/96-bit banks, registered reads, hold, reset retention and independent read/write passed");
}
#endif
