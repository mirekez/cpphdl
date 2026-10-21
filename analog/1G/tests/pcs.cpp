#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef VERILATOR
#include "VPCSSymbolMapper.h"
using DUT = VPCSSymbolMapper;
#else
#include "../pcs.hpp"
using DUT = PCSSymbolMapper;
long _system_clock = -1;
#endif
int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected test fixture directory");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open PCS trace");
        }
        DUT dut;
        unsigned point = 0, subset = 0, condition = 0, negate = 0;
#ifndef VERILATOR
        dut.point_in = _ASSIGN(point);
        dut.subset_in = _ASSIGN(subset);
        dut.condition_in = _ASSIGN(condition);
        dut.negate_in = _ASSIGN(negate);
        dut._assign();
#endif
        auto check = [&](unsigned expected, bool invalid, bool reset = false) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.point_in = point;
            dut.subset_in = subset;
            dut.condition_in = condition;
            dut.negate_in = negate;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            unsigned actual = dut.symbols_out;
            bool bad = dut.invalid_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            unsigned actual = unsigned(dut.symbols_out());
            bool bad = dut.invalid_out();
#endif
            if (trace.is_open())
                trace << actual << ',' << bad << '\n';
            if (expected != actual || invalid != bad)
                throw std::runtime_error("PCS symbol/control/sign mismatch condition=" + std::to_string(condition));
        };
        check(1170, false, true);
        unsigned count = 0;
        for (auto file : {"pcs_data.csv", "pcs_controls.csv"}) {
            std::ifstream input(std::string(argv[1]) + "/" + file);
            if (!input)
                throw std::runtime_error("missing PCS fixtures");
            std::string line;
            std::getline(input, line);
            while (std::getline(input, line)) {
                for (auto& ch : line)
                    if (ch == ',')
                        ch = ' ';
                std::istringstream row(line);
                unsigned first;
                std::array<int, 4> symbols{};
                if (!(row >> first >> subset >> symbols[0] >> symbols[1] >> symbols[2] >> symbols[3]))
                    throw std::runtime_error("bad PCS fixture");
                point = file == std::string("pcs_data.csv") ? first : 0;
                condition = file == std::string("pcs_data.csv") ? 0 : first;
                for (negate = 0; negate < 16; ++negate) {
                    unsigned expected = 0;
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        int symbol = (negate & (1 << lane)) ? -symbols[lane] : symbols[lane];
                        expected |= unsigned(symbol + 2) << (3 * lane);
                    }
                    check(expected, false);
                    ++count;
                }
            }
        }
        negate = 0;
        condition = 5;
        subset = 1;
        check(1170, true); // A delimiter cannot be mapped from an odd subset.
        condition = 12;
        subset = 0;
        point = 16;
        check(1170, true); // Idle only uses the first sixteen D0 points.
        condition = 15;
        check(1170, true);
        condition = 13;
        check(1170, false);
        std::cout << count << " PCS data/control/sign vectors and invalid-code/reset cases passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
