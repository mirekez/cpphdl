#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VERILATOR
#include "VPCSReceiver.h"
using DUT = VPCSReceiver;
#else
#include "../pcs_receiver.hpp"
using DUT = PCSReceiver;
long _system_clock = -1;
#endif

#include "receiver_fault_checks.hpp"
#include "receiver_enable_checks.hpp"

unsigned ethernet_crc(const std::vector<unsigned>& frame) {
    unsigned crc = 0xFFFFFFFF;
    for (unsigned index = 8; index + 4 < frame.size(); ++index) {
        crc ^= frame[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--enable")
        return receiver_enable_checks(argc - 1, argv + 1);
    if (argc > 1 && std::string(argv[1]) == "--faults")
        return receiver_fault_checks(argc - 1, argv + 1);
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected receiver fixtures [trace.csv]");
        std::ifstream vectors(argv[1]);
        if (!vectors)
            throw std::runtime_error("missing receiver fixtures");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open receiver trace");
        }
        DUT dut;
        unsigned samples = 0;
        bool master = false, pma = true;
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(samples);
        dut.remote_master_in = _ASSIGN(master);
        dut.pma_ok_in = _ASSIGN(pma);
        dut.symbol_enable_in = _ASSIGN(true);
        dut._assign();
#endif
        std::string line;
        unsigned cycles = 0, frames = 0, errored_frames = 0, bytes = 0, gap = 12;
        bool frame_error = false;
        std::vector<unsigned> frame;
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned long, 7> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed receiver fixture");
            bool reset = row[0];
            master = row[1];
            pma = row[2];
            samples = row[3];
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.symbol_enable_in = true;
            dut.remote_master_in = master;
            dut.pma_ok_in = pma;
            dut.samples_in = samples;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            unsigned word = dut.receive_out;
            bool locked = dut.locked_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            unsigned word = unsigned(dut.receive_out());
            bool locked = dut.locked_out();
#endif
            if (trace.is_open())
                trace << word << ',' << locked << '\n';
            if ((word & row[5]) != row[4] || (row[6] && !locked))
                throw std::runtime_error("receiver cycle " + std::to_string(cycles) + ": expected " +
                                         std::to_string(row[4]) + " mask " + std::to_string(row[5]) + ", got " +
                                         std::to_string(word) + " lock " + std::to_string(locked));
            if (word & 256) {
                if (frame.empty() && gap < 12)
                    throw std::runtime_error("receiver shortened inter-frame gap");
                frame.push_back(word & 255);
                frame_error |= (word & 512) != 0;
                gap = 0;
                ++bytes;
            } else {
                if (!frame.empty()) {
                    if (frame.size() != 72 && frame.size() != 1526)
                        throw std::runtime_error("receiver frame boundary/length mismatch");
                    for (unsigned index = 0; index < 8; ++index)
                        if (frame[index] != (index == 7 ? 0xD5 : 0x55))
                            throw std::runtime_error("receiver did not reconstruct preamble/SFD");
                    if (!frame_error) {
                        unsigned fcs = 0;
                        for (unsigned byte = 0; byte < 4; ++byte)
                            fcs |= frame[frame.size() - 4 + byte] << (8 * byte);
                        if (fcs != ethernet_crc(frame))
                            throw std::runtime_error("receiver output FCS mismatch");
                    }
                    ++frames;
                    errored_frames += frame_error;
                    frame.clear();
                    frame_error = false;
                }
                ++gap;
            }
            ++cycles;
        }
        if (frames != 64 || errored_frames != 12 || bytes != 51136 || !frame.empty())
            throw std::runtime_error("receiver fixture truncated or frame/error coverage lost");
        std::cout << frames << " complete IEEE-coded frames, " << bytes << " GMII bytes, " << errored_frames
                  << " explicit error frames; both roles and 16 pair polarities; preamble/FCS/IFG exact\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
