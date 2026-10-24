#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef VERILATOR
#include "VPCSReceiveSynchronizer.h"
using DUT = VPCSReceiveSynchronizer;
#else
#include "../pcs_synchronize.hpp"
using DUT = PCSReceiveSynchronizer;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected synchronization fixtures [trace.csv]");
        std::ifstream vectors(argv[1]);
        if (!vectors)
            throw std::runtime_error("missing synchronization fixtures");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open synchronization trace");
        }
        DUT dut;
        unsigned symbols = 0;
        bool master = false, pma = false, idle = false, restart = false;
#ifndef VERILATOR
        dut.symbols_in = _ASSIGN(symbols);
        dut.remote_master_in = _ASSIGN(master);
        dut.pma_ok_in = _ASSIGN(pma);
        dut.check_idle_in = _ASSIGN(idle);
        dut.restart_in = _ASSIGN(restart);
        dut.symbol_enable_in = _ASSIGN(true);
        dut._assign();
#endif
        std::string line;
        unsigned cycle = 0, locked_steps = 0;
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned long, 14> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed synchronization fixture");
            bool reset = row[0];
            master = row[1];
            pma = row[2];
            idle = row[3];
            restart = row[4];
            symbols = row[5];
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.symbol_enable_in = true;
            dut.remote_master_in = master;
            dut.pma_ok_in = pma;
            dut.check_idle_in = idle;
            dut.restart_in = restart;
            dut.symbols_in = symbols;
            dut.eval();
            dut.clk = 1;
            dut.eval();
#define READ_PORT(name) (unsigned long)(dut.name)
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
#define READ_PORT(name) (unsigned long)(dut.name())
#endif
            bool locked = READ_PORT(locked_out);
            std::array<unsigned long, 7> actual{READ_PORT(state_out),
                                                READ_PORT(sx_out),
                                                READ_PORT(sy_out),
                                                READ_PORT(sg_out),
                                                READ_PORT(low_mask_out),
                                                READ_PORT(polarity_out),
                                                READ_PORT(remote_receiver_ok_out)};
            if (trace.is_open()) {
                trace << locked;
                for (auto value : actual)
                    trace << ',' << value;
                trace << '\n';
            }
            if ((row[6] == 1 && !locked) || (row[6] == 2 && locked))
                throw std::runtime_error("synchronizer lock qualification at cycle " + std::to_string(cycle));
            if (row[6] == 1) {
                for (unsigned field = 0; field < actual.size(); ++field) {
                    if (field == 6 && !idle)
                        continue;
                    if (actual[field] != row[7 + field])
                        throw std::runtime_error(
                            "synchronizer cycle " + std::to_string(cycle) + " field " + std::to_string(field) +
                            ": expected " + std::to_string(row[7 + field]) + ", got " + std::to_string(actual[field]));
                }
                ++locked_steps;
            }
            ++cycle;
        }
        if (cycle < 80000 || locked_steps < 10000)
            throw std::runtime_error("truncated synchronization coverage");
        std::cout << cycle
                  << " unseeded receiver checks; both roles, 16 polarities, arbitrary seeds/phases, "
                     "frame tracking, remote status, PMA loss and stuck-signal rejection passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
