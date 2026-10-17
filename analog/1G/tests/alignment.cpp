#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VReceiveClockAligner.h"
using DUT = VReceiveClockAligner;
#else
#include "../receive_alignment.hpp"
using DUT = ReceiveClockAligner;
long _system_clock = -1;
#endif

namespace {
std::ofstream trace;
unsigned crc_step(unsigned crc, unsigned byte) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    return crc;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class Bench {
public:
    DUT dut;
#ifndef VERILATOR
    cpphdl::u<10> input = 0;
#endif
    Bench() {
#ifndef VERILATOR
        dut.receive_in = [this] { return &input; };
        dut._assign();
#endif
    }
    void rx(unsigned word, bool reset = false) {
#ifdef VERILATOR
        dut.reset = reset;
        dut.receive_in = word;
        dut.rx_clk = 0;
        dut.eval();
        dut.rx_clk = 1;
        dut.eval();
#else
        ++_system_clock;
        input = word;
        dut._work_rx_clk(reset);
        dut._strobe_rx_clk();
#endif
    }
    unsigned ref(bool reset = false) {
#ifdef VERILATOR
        dut.reset = reset;
        dut.ref_clk = 0;
        dut.eval();
        dut.ref_clk = 1;
        dut.eval();
        unsigned word = dut.aligned_out;
#else
        ++_system_clock;
        dut._work_ref_clk(reset);
        dut._strobe_ref_clk();
        unsigned word = unsigned(dut.aligned_out());
#endif
        if (trace.is_open())
            trace << word << ',' << fault() << ',' << overflow() << '\n';
        return word;
    }
    bool overflow() {
#ifdef VERILATOR
        return dut.overflow_out;
#else
        return dut.overflow_out();
#endif
    }
    bool fault() {
#ifdef VERILATOR
        return dut.fault_out;
#else
        return dut.fault_out();
#endif
    }
    void reset() {
        for (unsigned i = 0; i < 4; ++i) {
            rx(0, true);
            ref(true);
        }
    }
};
void crossing(double period, double phase) {
    Bench bench;
    bench.reset();
    std::vector<unsigned> stimulus;
    std::deque<unsigned> expected;
    unsigned expected_frames = 0, actual_frames = 0, gap = 100, bytes = 0;
    bool in_frame = false;
    std::vector<unsigned> received_frame;
    for (unsigned frame = 0; frame < 40; ++frame) {
        unsigned length = frame % 2 ? 1526 : 72;
        std::vector<unsigned> packet(length);
        unsigned crc = 0xffffffffu;
        for (unsigned i = 0; i < length - 4; ++i) {
            packet[i] = i < 7 ? 0x55 : i == 7 ? 0xd5 : ((frame * 73 + i * 19) & 255);
            if (i >= 8)
                crc = crc_step(crc, packet[i]);
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i)
            packet[length - 4 + i] = (crc >> (8 * i)) & 255;
        for (unsigned i = 0; i < length; ++i)
            stimulus.push_back(0x100 | packet[i] | (i == 43 ? 0x200 : 0));
        stimulus.insert(stimulus.end(), 12, 0);
    }
    stimulus.insert(stimulus.end(), 128, 0);
    unsigned index = 0;
    double rx_time = phase, ref_time = 0;
    while (index < stimulus.size() || !expected.empty() || in_frame) {
        if (index < stimulus.size() && rx_time < ref_time) {
            unsigned word = stimulus[index++];
            if (word & 256) {
                if (index == 1 || !(stimulus[index - 2] & 256))
                    ++expected_frames;
                expected.push_back(word);
            }
            bench.rx(word);
            rx_time += period;
        } else {
            unsigned word = bench.ref();
            require(!bench.fault() && !bench.overflow(), "unexpected clock-alignment fault");
            if (word & 256) {
                require(!expected.empty() && word == expected.front(), "crossing corrupted/reordered GMII byte");
                expected.pop_front();
                if (!in_frame) {
                    require(gap >= 12, "clock alignment shortened IFG below 12");
                    ++actual_frames;
                }
                in_frame = true;
                received_frame.push_back(word & 255);
                gap = 0;
                ++bytes;
            } else {
                if (in_frame) {
                    require(received_frame.size() == 72 || received_frame.size() == 1526,
                            "aligned frame length mismatch");
                    unsigned crc = 0xffffffffu;
                    for (unsigned i = 8; i < received_frame.size(); ++i)
                        crc = crc_step(crc, received_frame[i]);
                    require(crc == 0xdebb20e3u, "aligned Ethernet frame has invalid FCS residue");
                    received_frame.clear();
                }
                in_frame = false;
                ++gap;
            }
            ref_time += 1;
        }
        require(ref_time < 100000, "clock alignment failed to drain");
    }
    require(bytes == 31960 && actual_frames == expected_frames && actual_frames == 40, "frame/count mismatch");
}
void fault_cases() {
    Bench bench;
    bench.reset();
    for (unsigned length : {1, 2, 6}) {
        for (unsigned i = 0; i < length; ++i)
            bench.rx(0x1a5);
        bench.rx(0);
        unsigned received = 0;
        for (unsigned i = 0; i < 40; ++i) {
            bench.rx(0);
            unsigned word = bench.ref();
            if (word & 0x100) {
                require(word == 0x1a5, "short frame corruption");
                ++received;
            }
        }
        require(received == length && !bench.fault(), "short frame stranded below prefill threshold");
    }
    bench.reset();
    // A stopped destination must report overflow, rather than overwrite data.
    for (unsigned i = 0; i < 80; ++i)
        bench.rx(0x155);
    require(bench.overflow(), "FIFO did not report overflow");
    for (unsigned i = 0; i < 5; ++i)
        bench.ref();
    require(bench.fault(), "overflow did not reach the reference domain");
    for (unsigned i = 0; i < 4; ++i)
        require(bench.ref() == 0, "faulted receiver resumed without reset");
    bench.reset();
    require(!bench.fault() && !bench.overflow(), "joint reset did not clear fault");
    // A stopped recovered clock in the middle of a frame must signal truncation.
    for (unsigned i = 0; i < 20; ++i)
        bench.rx(0x1ab);
    bool error = false;
    for (unsigned i = 0; i < 60; ++i)
        error |= bench.ref() == 0x300;
    require(error && bench.fault(), "underflow did not mark the truncated frame erroneous");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            trace.open(argv[1]);
            require(bool(trace), "cannot open alignment trace");
        }
        for (double period : {.9998, 1., 1.0002})
            for (double phase : {.001, .49, .999})
                crossing(period, phase);
        fault_cases();
        std::cout << "9 phase/rate cases, 360 frames, 287640 bytes; FCS, IFG, overflow, underflow and reset passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
