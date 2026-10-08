#ifndef CPP_GRAPH_MEMORY_HEADER
#define CPP_GRAPH_MEMORY_HEADER "generated/CppGraphMemory.h"
#define CPP_GRAPH_MEMORY_TYPE CppGraphMemory
#endif
#include CPP_GRAPH_MEMORY_HEADER
#include "model.h"
#include <cstdio>
long _system_clock = 0;

int main() {
    CPP_GRAPH_MEMORY_TYPE ordinary;
    cpphdl_native::Model model;
    cpphdl::logic<1> reset;
    cpphdl::logic<2> request, write;
    cpphdl::array<2, cpphdl::logic<4>, true> address;
    cpphdl::array<2, cpphdl::logic<64>, true> data;
    cpphdl::array<2, cpphdl::logic<8>, true> mask;
    ordinary.rst_ni_in = _ASSIGN(reset);
    ordinary.req_i_in = _ASSIGN(request); ordinary.we_i_in = _ASSIGN(write);
    ordinary.addr_i_in = _ASSIGN(address); ordinary.wdata_i_in = _ASSIGN(data); ordinary.be_i_in = _ASSIGN(mask);
    uint64_t memory[16]{}, expected[2]{}, random = 0x1234567812345678ull;
    unsigned retained[2]{};
    for (unsigned sample = 0; sample < 4034; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        bool warmup = sample < 34;
        model.rst_ni[0] = sample != 0 && (warmup || sample % 79 != 0);
        model.req_i[0] = warmup ? 3 : (random >> 16) & 3;
        model.we_i[0] = warmup ? 3 : (random >> 18) & 3;
        model.addr_i[0] = warmup ? (sample % 16) * 17 : random & 255;
        model.be_i[0] = warmup ? 65535 : (random >> 24) & 65535;
        reset = model.rst_ni[0]; request = model.req_i[0]; write = model.we_i[0];
        address = model.addr_i[0]; mask = model.be_i[0];
        for (unsigned port = 0; port < 2; ++port) {
            uint64_t incoming = port ? ~random : random;
            data[port] = incoming;
            model.wdata_i[2 * port] = incoming; model.wdata_i[2 * port + 1] = incoming >> 32;
            unsigned selected = (model.addr_i[0] >> (port * 4)) & 15;
            if (!model.rst_ni[0]) retained[port] = 0;
            else {
                bool reading = ((model.req_i[0] >> port) & 1) && !((model.we_i[0] >> port) & 1);
                expected[port] = memory[reading ? selected : retained[port]];
                if (reading) retained[port] = selected;
            }
        }
        if (model.rst_ni[0]) for (unsigned port = 0; port < 2; ++port)
            if ((model.req_i[0] & model.we_i[0]) & (1u << port)) {
                unsigned selected = (model.addr_i[0] >> (port * 4)) & 15;
                uint64_t incoming = port ? ~random : random;
                for (unsigned byte = 0; byte < 8; ++byte) if (model.be_i[0] & (1u << (port * 8 + byte))) {
                    uint64_t lane = uint64_t(255) << (byte * 8);
                    memory[selected] = (memory[selected] & ~lane) | (incoming & lane);
                }
            }
        ++_system_clock; ordinary._work(false); ordinary._strobe(); ++_system_clock;
        model.step(); model.eval();
        if (!warmup) for (unsigned port = 0; port < 2; ++port) {
            uint64_t actual = uint64_t(cpphdl::logic<64>(ordinary.rdata_o_out()[port]));
            uint64_t native = uint64_t(model.rdata_o[2 * port]) | (uint64_t(model.rdata_o[2 * port + 1]) << 32);
            if (actual != expected[port] || native != expected[port]) {
                std::printf("converted SRAM mismatch sample=%u port=%u\n", sample, port); return 1;
            }
        }
    }
    std::puts("ordinary SV-to-C++ graph: 4000 dual-port SRAM transactions match C++ and independent oracle");
}
