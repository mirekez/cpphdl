#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef VERILATOR
#include "VPCSReceiveFramer.h"
using DUT = VPCSReceiveFramer;
#else
#include "../pcs_framer.hpp"
using DUT = PCSReceiveFramer;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected framer fixtures [trace.csv]");
        std::ifstream vectors(argv[1]);
        if (!vectors)
            throw std::runtime_error("missing framer fixtures");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open framer trace");
        }
        DUT dut;
        bool locked = true, pma = true;
        unsigned symbols = 0, sg = 0, sx = 0, mask = 0, polarity = 0;
#ifndef VERILATOR
        dut.symbols_in = _ASSIGN(symbols);
        dut.sg_in = _ASSIGN(sg);
        dut.sx_in = _ASSIGN(sx);
        dut.low_mask_in = _ASSIGN(mask);
        dut.polarity_in = _ASSIGN(polarity);
        dut.locked_in = _ASSIGN(locked);
        dut.pma_ok_in = _ASSIGN(pma);
        dut._assign();
#endif
        std::string line;
        unsigned cycle = 0;
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned, 13> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed framer fixture");
            bool reset = row[0];
            locked = row[1];
            pma = row[2];
            symbols = row[3];
            sg = row[4];
            sx = row[5];
            mask = row[6];
            polarity = row[7];
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.symbols_in = symbols;
            dut.sg_in = sg;
            dut.sx_in = sx;
            dut.low_mask_in = mask;
            dut.polarity_in = polarity;
            dut.locked_in = locked;
            dut.pma_ok_in = pma;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            std::array<unsigned, 5> actual{dut.tag_out, dut.control_out, dut.negate_out, dut.restart_out,
                                           dut.remote_receiver_ok_out};
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            std::array<unsigned, 5> actual{unsigned(dut.tag_out()), unsigned(dut.control_out()),
                                           unsigned(dut.negate_out()), unsigned(dut.restart_out()),
                                           unsigned(dut.remote_receiver_ok_out())};
#endif
            for (unsigned field = 0; field < 5; ++field) {
                if (trace.is_open())
                    trace << (field ? "," : "") << actual[field];
                if (actual[field] != row[field + 8])
                    throw std::runtime_error("framer mismatch at cycle " + std::to_string(cycle) + " field " +
                                             std::to_string(field));
            }
            if (trace.is_open())
                trace << '\n';
            ++cycle;
        }
        if (cycle < 3000)
            throw std::runtime_error("truncated framer fixtures");
        std::cout << cycle << " framer tag/control/sign observations, deferred SSD errors and PMA loss passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
