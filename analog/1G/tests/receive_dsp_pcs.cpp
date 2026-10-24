#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VERILATOR
#include "VReceiveDSPPCS.h"
using DUT = VReceiveDSPPCS;
#else
#include "../receive_dsp_pcs.hpp"
using DUT = ReceiveDSPPCS;
long _system_clock = -1;
#endif

namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

void run(const std::string& path, unsigned test, std::ofstream& trace) {
    std::ifstream input(path);
    unsigned master, count, mapping, delays, frame_count;
    require(bool(input >> master >> count >> mapping >> delays >> frame_count), "missing fixture header");
    std::vector<std::array<unsigned, 5>> samples(count);
    for (auto& sample : samples)
        for (auto& value : sample)
            require(bool(input >> value), "truncated fixed channel");
    std::vector<std::vector<unsigned>> frames(frame_count);
    for (auto& frame : frames) {
        unsigned size;
        require(bool(input >> size), "missing independent frame truth");
        frame.resize(size);
        for (auto& byte : frame)
            require(bool(input >> byte), "truncated frame truth");
    }
    DUT dut;
    unsigned word = 0x80808080, local = 1170, desired = 1170, tag = 0;
    bool valid = false, train = false, enable = true, signal = true, transport_fault = false, restart = false;
#ifndef VERILATOR
    dut.samples_in = _ASSIGN(word);
    dut.local_in = _ASSIGN(local);
    dut.desired_in = _ASSIGN(desired);
    dut.tag_in = _ASSIGN(tag);
    dut.clipped_in = _ASSIGN(false);
    dut.train_in = _ASSIGN(train);
    dut.valid_in = _ASSIGN(valid);
    dut.enable_in = _ASSIGN(enable);
    dut.restart_in = _ASSIGN(restart);
    dut.signal_ready_in = _ASSIGN(signal);
    dut.transport_fault_in = _ASSIGN(transport_fault);
    dut.remote_master_in = _ASSIGN(master != 0);
    dut._assign();
#endif
    unsigned cycle = 0, completed = 0, bytes = 0, gaps_with_lock = 0;
    bool acquired = false, qualified = false;
    std::vector<unsigned> frame;
    auto edge = [&](bool reset, bool withdraw = false) {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.samples_in = word;
        dut.local_in = local;
        dut.desired_in = desired;
        dut.tag_in = tag;
        dut.clipped_in = false;
        dut.train_in = train;
        dut.valid_in = valid;
        dut.enable_in = enable;
        dut.restart_in = restart;
        dut.signal_ready_in = signal;
        dut.transport_fault_in = transport_fault;
        dut.remote_master_in = master;
        dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
        const unsigned output = READ(receive_out), output_valid = READ(receive_valid_out),
                       lock = READ(locked_out), quality = READ(quality_out),
                       pair_lock = READ(pairs_locked_out), map = READ(pair_mapping_out),
                       taps = READ(pair_delays_out), busy = READ(training_busy_out), fault = READ(fault_out),
                       remote_ok = READ(remote_receiver_ok_out);
#undef READ
        if (!reset) {
            trace << test << ',' << cycle << ',' << (output_valid ? output : 0) << ',' << output_valid << ','
                  << lock << ',' << quality << ',' << pair_lock << ',' << (pair_lock ? map : 0) << ','
                  << (pair_lock ? taps : 0) << ',' << busy << ',' << fault << ',' << remote_ok << '\n';
            require(fault == unsigned(transport_fault), "unexpected composed receiver fault");
            if (withdraw) {
                require(!pair_lock && !lock && !quality && !output_valid && !remote_ok,
                        "receiver retained availability after control withdrawal");
            } else if (train || busy) {
                require(!pair_lock && !lock && !quality && !output_valid,
                        "receiver qualified while training updates were pending");
            } else if (pair_lock) {
                require(map == mapping && taps == delays, "wrong acquired cable identity/skew");
                acquired = true;
                gaps_with_lock += !valid;
            } else {
                require(!acquired, "input bubble revoked acquired cable state");
            }
            qualified |= quality;
            if (output_valid && !withdraw) {
                // Startup may emit errors before PCS lock. No bytes are allowed
                // until quality has been established from actual clean idles.
                if (qualified)
                    require(!(output & 512) && quality, "qualified receiver lost clean decoding");
                if (output & 256) {
                    require(quality, "frame delivered before clean idle qualification");
                    frame.push_back(output & 255);
                } else if (!frame.empty()) {
                    require(completed < frames.size() && frame == frames[completed],
                            "decoded Ethernet frame differs from independent truth");
                    bytes += frame.size();
                    ++completed;
                    frame.clear();
                }
            }
        }
#ifdef VERILATOR
        dut.clk = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++cycle;
    };
    edge(true);
    for (unsigned n = 0; n < count; ++n) {
        word = samples[n][0];
        local = samples[n][1];
        desired = samples[n][2];
        tag = samples[n][3];
        train = samples[n][4];
        valid = true;
        edge(false);
        // Average two core clocks per symbol, with occasional adjacent ticks
        // and longer gaps. No unaccepted input may change symbol histories.
        valid = false;
        local = 4095;
        desired = 4095;
        if (n % 97 != 0)
            edge(false);
        if (n % 97 == 1)
            edge(false);
    }
    require(acquired && qualified && gaps_with_lock > 1000, "missing acquisition/quality/gap coverage");
    require(frame.empty() && completed == frame_count && bytes == 1598, "missing Ethernet frames");
    // Every availability input must withdraw already-established link status
    // combinationally, before stale pipeline words can escape.
    signal = test != 0;
    enable = test != 1;
    transport_fault = test == 2;
    restart = test == 3;
    edge(false, true);
    std::cout << "case " << test << " master=" << master << " frames=" << completed << " bytes=" << bytes
              << " clocks=" << cycle << " PASS\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture directory and trace destination");
        std::ofstream trace(argv[2]);
        require(bool(trace), "cannot open trace");
        for (unsigned test = 0; test < 4; ++test)
            run(std::string(argv[1]) + "/case" + std::to_string(test) + ".txt", test, trace);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
