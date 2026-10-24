#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef VERILATOR
#ifdef TEST_BITS
#include "VPCSTransmitBits.h"
using DUT = VPCSTransmitBits;
#else
#include "VPCSTransmitter.h"
using DUT = VPCSTransmitter;
#endif
#else
#include "../pcs_transmit.hpp"
#ifdef TEST_BITS
using DUT = PCSTransmitBits;
#else
using DUT = PCSTransmitter;
#endif
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected independent transmit fixture file [trace.csv]");
        std::ifstream vectors(argv[1]);
        if (!vectors)
            throw std::runtime_error("missing transmit fixtures");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open transmit trace");
        }
        DUT dut;
        unsigned data = 0, mode = 0;
        unsigned long seed = 1;
        bool enable = false, error = false, master = false, link = false, local = false, load = false;
#ifndef VERILATOR
        dut.data_in = _ASSIGN(data);
        dut.enable_in = _ASSIGN(enable);
        dut.error_in = _ASSIGN(error);
        dut.master_in = _ASSIGN(master);
        dut.link_in = _ASSIGN(link);
        dut.local_receiver_ok_in = _ASSIGN(local);
        dut.mode_in = _ASSIGN(mode);
        dut.seed_load_in = _ASSIGN(load);
        dut.seed_in = _ASSIGN(seed);
        dut._assign();
#endif
        std::string line;
        unsigned cycle = 0, previous_packed = 1170;
        std::array<unsigned, 14> coverage{};
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned long, 20> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed transmit vector");
            bool reset = row[0];
            load = row[1];
            seed = row[2];
            master = row[3];
            mode = row[4];
            link = row[5];
            local = row[6];
            enable = row[7];
            error = row[8];
            data = row[9];
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.seed_load_in = load;
            dut.seed_in = seed;
            dut.master_in = master;
            dut.mode_in = mode;
            dut.link_in = link;
            dut.local_receiver_ok_in = local;
            dut.enable_in = enable;
            dut.error_in = error;
            dut.data_in = data;
            dut.eval();
            dut.clk = 1;
            dut.eval();
#define READ_PORT(name) unsigned(dut.name)
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
#define READ_PORT(name) unsigned(dut.name())
#endif
#ifdef TEST_BITS
            std::array<unsigned, 9> actual{READ_PORT(sx_out),        READ_PORT(sy_out),      READ_PORT(sg_out),
                                           READ_PORT(encoded_out),   READ_PORT(trellis_out), READ_PORT(subset_out),
                                           READ_PORT(condition_out), READ_PORT(negate_out),  READ_PORT(fault_out)};
            for (unsigned field = 0; field < actual.size(); ++field) {
                if (trace.is_open())
                    trace << (field ? "," : "") << actual[field];
                if (actual[field] != row[10 + field])
                    throw std::runtime_error("transmit bits cycle " + std::to_string(cycle) + " field " +
                                             std::to_string(field) + ": expected " + std::to_string(row[10 + field]) +
                                             ", got " + std::to_string(actual[field]));
            }
#else
            unsigned actual = READ_PORT(symbols_out), fault = READ_PORT(fault_out);
            unsigned expected = reset ? 1170 : previous_packed;
            if (trace.is_open())
                trace << actual << ',' << fault;
            if (actual != expected || fault != row[18])
                throw std::runtime_error("transmitter cycle " + std::to_string(cycle) + ": expected symbols " +
                                         std::to_string(expected) + ", got " + std::to_string(actual));
#endif
            if (trace.is_open())
                trace << '\n';
            previous_packed = row[19];
            ++coverage.at(row[16]);
            ++cycle;
        }
        if (cycle < 50000)
            throw std::runtime_error("transmit fixture was truncated");
        for (unsigned condition = 0; condition < coverage.size(); ++condition)
            if (!coverage[condition])
                throw std::runtime_error("uncovered transmit condition " + std::to_string(condition));
        std::cout << cycle
                  << " independent transmit vectors; all 14 conditions, both roles, basis states, "
                     "framing, errors, training, seed and re-arm checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
