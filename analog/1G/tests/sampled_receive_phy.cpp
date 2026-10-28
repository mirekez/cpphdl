#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(CLOCKED_PHY) && defined(VERILATOR)
#include "VClockedReceivePHY.h"
using DUT = VClockedReceivePHY;
#elif defined(CLOCKED_PHY)
#include "../clocked_receive_phy.hpp"
using DUT = ClockedReceivePHY;
long _system_clock = -1;
#elif defined(ADC_PHY) && defined(VERILATOR)
#include "VADCReceivePHY.h"
using DUT = VADCReceivePHY;
#elif defined(ADC_PHY)
#include "../adc_receive_phy.hpp"
using DUT = ADCReceivePHY;
long _system_clock = -1;
#elif defined(VERILATOR)
#include "VSampledReceivePHY.h"
using DUT = VSampledReceivePHY;
#else
#include "../sampled_receive_phy.hpp"
using DUT = SampledReceivePHY;
long _system_clock = -1;
#endif

namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

void run(const std::string& path, unsigned test, std::ofstream& trace, bool track) {
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
#ifdef ADC_PHY
    std::array<unsigned, 64> held{};
    std::array<unsigned, 4> comparisons{};
    bool sampling = true;
#endif
    unsigned word = 0x80808080, local = 1170, desired = 1170, tag = 0;
    bool valid = false, train = false, enable = true, signal = true, transport_fault = false, restart = false, clock_ready = true;
#ifndef VERILATOR
#ifdef ADC_PHY
    for (unsigned i = 0; i < 4; ++i) {
        dut.comparisons_in[i] = _ASSIGN_I(comparisons[i]);
        dut.offset_in[i] = _ASSIGN(128);
        dut.gain_in[i] = _ASSIGN(256);
    }
    dut.time_in = _ASSIGN(tag);
    dut.sample_enable_in = _ASSIGN(sampling);
#else
    dut.samples_in = _ASSIGN(word);
    dut.sample_tag_in = _ASSIGN(tag);
    dut.clipped_in = _ASSIGN(false);
    dut.sample_valid_in = _ASSIGN(valid);
#endif
    dut.local_in = _ASSIGN(local);
    dut.desired_in = _ASSIGN(desired);
    dut.train_in = _ASSIGN(train);
    dut.enable_in = _ASSIGN(enable);
    dut.restart_in = _ASSIGN(restart);
    dut.signal_ready_in = _ASSIGN(signal);
    dut.transport_fault_in = _ASSIGN(transport_fault);
    dut.remote_master_in = _ASSIGN(master != 0);
    dut.track_in = _ASSIGN(track);
    dut.initial_phase_in = _ASSIGN(track ? test * 4194304 : 0);
    dut.origin_in = _ASSIGN(65520);
#ifdef CLOCKED_PHY
    dut.power_good_in = _ASSIGN(clock_ready);
#else
    dut.clock_ready_in = _ASSIGN(clock_ready);
#endif
    dut._assign();
#endif
    unsigned cycle = 0, completed = 0, bytes = 0, gaps_with_lock = 0;
    bool acquired = false, qualified = false;
    std::vector<unsigned> frame;
    auto edge = [&](bool reset, bool withdraw = false) {
#ifdef VERILATOR
#ifdef CLOCKED_PHY
        dut.adc_clk = 0;
        dut.ref_clk = 0;
#else
        dut.clk = 0;
#endif
        dut.reset = reset;
#ifdef ADC_PHY
        dut.time_in = tag;
        dut.sample_enable_in = sampling;
        for (unsigned i = 0; i < 4; ++i) {
            dut.offset_in[i] = 128;
            dut.gain_in[i] = 256;
            dut.comparisons_in[i] = comparisons[i];
        }
#else
        dut.samples_in = word;
        dut.sample_tag_in = tag;
        dut.clipped_in = false;
        dut.sample_valid_in = valid;
#endif
        dut.local_in = local;
        dut.desired_in = desired;
        dut.train_in = train;
        dut.enable_in = enable;
        dut.restart_in = restart;
        dut.signal_ready_in = signal;
        dut.transport_fault_in = transport_fault;
        dut.remote_master_in = master;
        dut.track_in = track;
        dut.initial_phase_in = track ? test * 4194304 : 0;
        dut.origin_in = 65520;
#ifdef CLOCKED_PHY
        dut.power_good_in = clock_ready;
#else
        dut.clock_ready_in = clock_ready;
#endif
        dut.eval();
#define READ(name) unsigned(dut.name)
#else
        ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
#ifdef ADC_PHY
        // The independent input is sampled only when a real SAR switch opens.
        // Subsequent comparisons use that held voltage and each actual trial.
        const unsigned aperture = READ(aperture_out);
        if (valid && !withdraw)
            require(aperture != 0, "missing physical sampling aperture");
        comparisons.fill(0);
        for (unsigned i = 0; i < 64; ++i) {
            if ((aperture >> (i % 16)) & 1)
                held[i] = (word >> (8 * (i / 16))) & 255;
            if (held[i] >= READ(trial_out[i]))
                comparisons[i / 16] |= 1u << (i % 16);
        }
#ifdef VERILATOR
        for (unsigned i = 0; i < 4; ++i)
            dut.comparisons_in[i] = comparisons[i];
        dut.eval();
#else
        ++_system_clock;
#endif
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
            require(fault == unsigned(transport_fault), "unexpected sampled receiver fault at clock " + std::to_string(cycle));
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
#ifdef CLOCKED_PHY
        dut.adc_clk = 1;
        dut.ref_clk = (cycle % 2) == 0;
#else
        dut.clk = 1;
#endif
        dut.eval();
#else
#ifdef CLOCKED_PHY
        dut._work_adc_clk(reset);
        if (cycle % 2 == 0) dut._work_ref_clk(reset);
        dut._strobe_adc_clk();
        if (cycle % 2 == 0) dut._strobe_ref_clk();
#else
        dut._work(reset);
        dut._strobe();
#endif
#endif
        ++cycle;
    };
    edge(true);
#ifdef ADC_PHY
    // 4096 precharge + four release clocks, followed by the three-clock SAR
    // acquisition sequence. The first fixture point is the first aperture.
#ifdef CLOCKED_PHY
    sampling = false;
    for (unsigned warmup = 0; warmup < 32000; ++warmup)
        edge(false);
    sampling = true;
    for (unsigned warmup = 0; warmup < 3; ++warmup)
        edge(false);
#else
    for (unsigned warmup = 0; warmup < 4103; ++warmup)
        edge(false);
#endif
#endif
    for (unsigned n = 0; n < count; ++n) {
        for (unsigned half = 0; half < 2; ++half) {
            word = samples[n][0];
            if (half && n + 1 < count) {
                word = 0;
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const unsigned first = (samples[n][0] >> (8 * lane)) & 255;
                    const unsigned second = (samples[n + 1][0] >> (8 * lane)) & 255;
                    word |= ((first + second + 1) / 2) << (8 * lane);
                }
            }
            local = samples[n][1];
            desired = samples[n][2];
            tag = (65520 + 2 * n + half) & 65535;
            train = samples[n][4];
            valid = true;
            edge(false);
        }
    }
#ifdef ADC_PHY
    sampling = false;
    valid = false;
    train = false;
    for (unsigned flush = 0; flush < 64; ++flush) {
        tag = (tag + 1) & 65535;
        edge(false);
    }
#endif
    require(acquired && qualified, "missing acquisition/quality/gap coverage");
    require(frame.empty() && completed == frame_count && bytes == 1598, "missing Ethernet frames");
    // Every availability input must withdraw already-established link status
    // combinationally, before stale pipeline words can escape.
    signal = test != 0;
    clock_ready = test != 1;
    transport_fault = test == 2;
    restart = test == 3;
    edge(false, true);
    std::cout << "case " << test << " master=" << master << " frames=" << completed << " bytes=" << bytes
              << " clocks=" << cycle << " PASS\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "expected fixture directory, trace destination and optional tracking flag");
        std::ofstream trace(argv[2]);
        require(bool(trace), "cannot open trace");
        for (unsigned test = 0; test < 4; ++test)
            run(std::string(argv[1]) + "/case" + std::to_string(test) + ".txt", test, trace, argc == 4);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
