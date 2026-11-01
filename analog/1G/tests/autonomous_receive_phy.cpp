#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VERILATOR
#include "VAutonomousReceivePHY.h"
using DUT = VAutonomousReceivePHY;
#define READ(name) unsigned(dut.name)
#else
#include "../autonomous_receive_phy.hpp"
using DUT = AutonomousReceivePHY;
long _system_clock = -1;
#define READ(name) unsigned(dut.name())
#endif

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void run(const std::string& path, unsigned test, std::ofstream& trace) {
    std::ifstream input(path);
    unsigned master, count, mapping, delays, frame_count;
    require(bool(input >> master >> count >> mapping >> delays >> frame_count), "missing fixture header");
    std::vector<std::array<unsigned, 2>> samples(count);
    for (auto& sample : samples)
        for (auto& value : sample)
            require(bool(input >> value), "truncated channel");
    std::vector<std::vector<unsigned>> frames(frame_count);
    for (auto& frame : frames) {
        unsigned size;
        require(bool(input >> size), "missing frame truth");
        frame.resize(size);
        for (auto& byte : frame) require(bool(input >> byte), "truncated frame truth");
    }
    DUT dut;
    unsigned word = 0x80808080, local = 1170, tag = 65520;
    bool valid = false, ready = true, signal = true, transport_fault = false, restart = false;
#ifndef VERILATOR
    dut.clear_adc_in = _ASSIGN(false);
    dut.clear_pcs_in = _ASSIGN(false);
    dut.samples_in = _ASSIGN(word);
    dut.local_in = _ASSIGN(local);
    dut.sample_tag_in = _ASSIGN(tag);
    dut.sample_valid_in = _ASSIGN(valid);
    dut.clipped_in = _ASSIGN(false);
    dut.track_in = _ASSIGN(true);
    dut.initial_phase_in = _ASSIGN(test * 4194304);
    dut.origin_in = _ASSIGN(65520);
    dut.enable_in = _ASSIGN(true);
    dut.restart_in = _ASSIGN(restart);
    dut.clock_ready_in = _ASSIGN(true);
    dut.processing_ready_in = _ASSIGN(ready);
    dut.signal_ready_in = _ASSIGN(signal);
    dut.transport_fault_in = _ASSIGN(transport_fault);
    dut.remote_master_in = _ASSIGN(master);
    dut._assign();
#endif
    unsigned events = 0, completed = 0, bytes = 0;
    bool acquired = false, qualified = false;
    std::vector<unsigned> frame;
    auto edge = [&](bool adc, bool pcs, bool reset, bool withdraw = false) {
#ifdef VERILATOR
        dut.clear_adc_in = false;
        dut.clear_pcs_in = false;
        dut.adc_clk = 0; dut.pcs_clk = 0;
        dut.reset = reset;
        dut.samples_in = word; dut.local_in = local;
        dut.sample_tag_in = tag; dut.sample_valid_in = valid;
        dut.clipped_in = false; dut.track_in = true;
        dut.initial_phase_in = test * 4194304; dut.origin_in = 65520;
        dut.enable_in = true; dut.restart_in = restart; dut.clock_ready_in = true;
        dut.processing_ready_in = ready; dut.signal_ready_in = signal;
        dut.transport_fault_in = transport_fault; dut.remote_master_in = master;
        dut.eval();
#else
        ++_system_clock;
#endif
        const unsigned output = READ(receive_out), output_valid = READ(receive_valid_out),
                       lock = READ(locked_out), quality = READ(quality_out), pairs = READ(pairs_locked_out),
                       map = READ(pair_mapping_out), taps = READ(pair_delays_out), busy = READ(acquiring_out);
        if (!reset) {
            trace << test << ',' << events << ',' << adc << ',' << pcs << ','
                  << (output_valid ? output : 0) << ',' << output_valid << ',' << lock << ',' << quality << ','
                  << pairs << ',' << (pairs ? map : 0) << ',' << (pairs ? taps : 0) << ',' << busy << ','
                  << READ(fault_out) << '\n';
            require(READ(fault_out) == unsigned(transport_fault), "unexpected asynchronous receive fault");
            if (withdraw) {
                require(!lock && !quality && !pairs && !output_valid && !READ(remote_receiver_ok_out),
                        "stale receiver state escaped clock/signal withdrawal");
            } else if (pcs) {
                if (pairs) {
                    require(map == mapping && taps == delays, "wrong acquired cable mapping/skew");
                    acquired = true;
                } else {
                    require(!acquired, "processor FIFO gap revoked cable acquisition");
                }
                qualified |= quality;
                if (output_valid) {
                    if (qualified)
                        require(!(output & 512) && quality, "qualified asynchronous receiver lost decoding");
                    if (output & 256) {
                        require(quality, "frame escaped before clean-idle qualification");
                        frame.push_back(output & 255);
                    } else if (!frame.empty()) {
                        require(completed < frames.size() && frame == frames[completed], "independent Ethernet frame mismatch");
                        bytes += frame.size(); ++completed; frame.clear();
                    }
                }
            }
        }
#ifdef VERILATOR
        dut.adc_clk = adc; dut.pcs_clk = pcs; dut.eval();
#else
        if (adc) dut._work_adc_clk(reset);
        if (pcs) dut._work_pcs_clk(reset);
        if (adc) dut._strobe_adc_clk();
        if (pcs) dut._strobe_pcs_clk();
#endif
        ++events;
    };
    edge(true, true, true);
    unsigned next_adc = 0, next_pcs = test * 751, index = 0;
    const unsigned pcs_period = test % 2 ? 6944 : 7576;
    while (std::min(next_adc, next_pcs) < (2 * count + 64) * 4000) {
        const unsigned time = std::min(next_adc, next_pcs);
        const bool adc = time == next_adc, pcs = time == next_pcs;
        if (adc) {
            valid = index < 2 * count;
            if (valid) {
                const unsigned n = index / 2;
                word = samples[n][0];
                if (index % 2 && n + 1 < count) {
                    word = 0;
                    for (unsigned lane = 0; lane < 4; ++lane)
                        word |= ((((samples[n][0] >> (8 * lane)) & 255) +
                                  ((samples[n + 1][0] >> (8 * lane)) & 255) + 1) / 2) << (8 * lane);
                }
                local = samples[n][1];
                tag = (65520 + index) & 65535;
            }
            ++index; next_adc += 4000;
        }
        if (pcs) next_pcs += pcs_period;
        edge(adc, pcs, false);
    }
    require(acquired && qualified && frame.empty() && completed == frame_count && bytes == 1598,
            "missing asynchronous Ethernet frame coverage");
    signal = test != 0; ready = test != 1; transport_fault = test == 2; restart = test == 3;
    edge(true, true, false, true);
    std::cout << "case " << test << " master=" << master << " frames=" << completed << " bytes=" << bytes
              << " events=" << events << " PASS\n";
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture directory and trace path");
        std::ofstream trace(argv[2]);
        require(bool(trace), "cannot open trace");
        for (unsigned test = 0; test < 4; ++test)
            run(std::string(argv[1]) + "/case" + std::to_string(test) + ".txt", test, trace);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
