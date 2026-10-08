#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef RETIMING_GRAPH
#include "model.h"
#define DRIVE(name, value) dut.name[0] = value
#define GET(name) dut.name[0]
#else
#include "VSynthRetiming.h"
#define DRIVE(name, value) dut.name = value
#define GET(name) dut.name
#endif
long _system_clock = 0;
int main() {
#ifdef RETIMING_GRAPH
    cpphdl_native::Model dut;
#else
    VSynthRetiming dut;
#endif
    uint8_t a = 0, b = 0, tag = 0, address = 0;
    uint16_t data = 0, bias = 0;
    bool write = false, read = false;
    std::vector<unsigned> results, tags, mirrors, partials;
    std::array<uint16_t, 8> memory{};
    unsigned first = 0, tag1 = 0, age = 0;
#ifdef RAM_RETIMING
    cpphdl_top.address_in = _ASSIGN(address); cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.bias_in = _ASSIGN(bias); cpphdl_top.write_in = _ASSIGN(write); cpphdl_top.read_in = _ASSIGN(read);
#else
    cpphdl_top.a_in = _ASSIGN(a); cpphdl_top.b_in = _ASSIGN(b);
    cpphdl_top._assign();
#endif
    cpphdl_top.tag_value_in = _ASSIGN(tag);
    for (unsigned t = 0; t < 2000; ++t) {
        bool reset = t == 0 || (t > 40 && t % 101 == 0);
        unsigned expected = 0, expectedTag = 0;
        a = uint8_t(t * 13); b = uint8_t(t * 97); tag = uint8_t(t * 7);
        address = t % 8; data = uint16_t(t * 317); bias = uint16_t(t * 113);
        write = t % 5 != 0 || t < 32; read = t > 32 && t % 7 != 0;
        DRIVE(work_reset, reset); DRIVE(tag_value, tag);
#ifdef RAM_RETIMING
        DRIVE(address, address); DRIVE(data, data); DRIVE(bias, bias); DRIVE(write, write); DRIVE(read, read);
        if (read) {
            uint16_t x = uint16_t(memory[address] + bias);
            x ^= 0x1379; x = uint16_t(x + 23); x ^= uint16_t(x << 1); expected = uint16_t(x + 41);
        }
        if (write && !reset) memory[address] = data;
        expectedTag = tag;
#else
        DRIVE(a, a); DRIVE(b, b);
        uint8_t x = uint8_t(first + 13);
        x ^= 0x59; x = uint8_t(x + 29); x ^= uint8_t(x << 1); expected = uint8_t(x + 31);
        first = uint8_t(a + b); expectedTag = tag1; tag1 = tag;
#endif
        if (reset) { expected = 0; expectedTag = 0x5a; first = 0; tag1 = 0x5a; age = 0; }
        else ++age;
        cpphdl_top._work(reset); cpphdl_top._strobe();
        if (unsigned(cpphdl_top.result_out()) != expected || unsigned(cpphdl_top.tag_out()) != expectedTag) {
            std::fprintf(stderr, "original C++ mismatch at %u\n", t); return 1;
        }
        results.push_back(expected); tags.push_back(expectedTag); mirrors.push_back(reset ? 0 : a);
        partials.push_back(first);
#ifdef RETIMING_GRAPH
        dut.step();
#else
        dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#endif
        // Added pipeline contents are invalid while filling after reset.
        if (RETIMING_LATENCY == 0 || (age > RETIMING_LATENCY + 3 && t > 40)) {
            auto sample = t - RETIMING_LATENCY;
            if (GET(result) != results[sample] || GET(tag) != tags[sample]) {
                std::fprintf(stderr, "retiming mismatch at %u latency=%u: result %u/%u tag %u/%u\n", t,
                    unsigned(RETIMING_LATENCY), unsigned(GET(result)), results[sample], unsigned(GET(tag)), tags[sample]); return 1;
            }
#ifndef RAM_RETIMING
            if (GET(partial) != partials[sample]) { std::fprintf(stderr, "intermediate output alignment changed at %u\n", t); return 1; }
            auto mirrorSample = RETIMING_SCOPED ? t : sample;
            if (GET(mirror) != mirrors[mirrorSample]) { std::fprintf(stderr, "module boundary changed at %u\n", t); return 1; }
#endif
        }
        if (reset && (GET(result) != 0 || GET(tag) != 0x5a)) { std::fprintf(stderr, "reset priority lost\n"); return 1; }
        ++_system_clock;
    }
    std::puts("2000 transactions: arithmetic, sideband alignment, resets and memory ordering passed");
}
