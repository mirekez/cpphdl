#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef VERILATOR
#ifdef QUALIFIED_ADC_PHY
#include "VQualifiedADCReceivePHY.h"
using DUT = VQualifiedADCReceivePHY;
#else
#include "VAutonomousADCReceivePHY.h"
using DUT = VAutonomousADCReceivePHY;
#endif
#define READ(name) unsigned(dut.name)
#else
#ifdef QUALIFIED_ADC_PHY
#include "../qualified_adc_receive_phy.hpp"
using DUT = QualifiedADCReceivePHY;
#else
#include "../autonomous_adc_receive_phy.hpp"
using DUT = AutonomousADCReceivePHY;
#endif
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
    std::array<unsigned, 64> held{};
    std::array<unsigned, 4> comparisons{};
    bool sampling = true;
    unsigned word = 0x80808080, local = 1170, tag = 65520;
    bool valid = false, ready = true, signal = true, transport_fault = false, restart = false;
    bool power_good = true, was_withdrawn = false;
#ifndef VERILATOR
#ifdef QUALIFIED_ADC_PHY
    dut.power_good_in = _ASSIGN(power_good);
#else
    dut.clear_adc_in = _ASSIGN(false);
    dut.clear_pcs_in = _ASSIGN(false);
#endif
    for (unsigned i = 0; i < 4; ++i) {
        dut.comparisons_in[i] = _ASSIGN_I(comparisons[i]);
        dut.offset_in[i] = _ASSIGN(128);
        dut.gain_in[i] = _ASSIGN(256);
    }
    dut.time_in = _ASSIGN(tag);
    dut.sample_enable_in = _ASSIGN(sampling);
    dut.local_in = _ASSIGN(local);
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
#ifdef QUALIFIED_ADC_PHY
        const bool withdrawn = reset || !power_good || !ready || restart;
        const bool ref = adc && (events % 2 == 0);
#endif
#ifdef VERILATOR
#ifdef QUALIFIED_ADC_PHY
        dut.power_good_in = power_good;
        dut.ref_clk = 0;
#else
        dut.clear_adc_in = false;
        dut.clear_pcs_in = false;
#endif
        dut.adc_clk = 0; dut.pcs_clk = 0;
        dut.reset = reset;
        dut.local_in = local; dut.time_in = tag; dut.sample_enable_in = sampling;
        for (unsigned i = 0; i < 4; ++i) {
            dut.comparisons_in[i] = comparisons[i];
            dut.offset_in[i] = 128; dut.gain_in[i] = 256;
        }
        dut.track_in = true;
        dut.initial_phase_in = test * 4194304; dut.origin_in = 65520;
        dut.enable_in = true; dut.restart_in = restart; dut.clock_ready_in = true;
        dut.processing_ready_in = ready; dut.signal_ready_in = signal;
        dut.transport_fault_in = transport_fault; dut.remote_master_in = master;
        dut.eval();
#else
        ++_system_clock;
#ifdef QUALIFIED_ADC_PHY
        if (withdrawn && !was_withdrawn) {
            dut.sim_assert_ref_reset(); dut.sim_assert_adc_reset(); dut.sim_assert_pcs_reset();
            dut._strobe_ref_clk(); dut._strobe_adc_clk(); dut._strobe_pcs_clk();
            ++_system_clock;
        }
#endif
#endif
        if (adc && !reset) {
            const unsigned aperture = READ(aperture_out);
            if (valid && sampling)
                require(aperture != 0, "missing sampling aperture");
            comparisons.fill(0);
            for (unsigned i = 0; i < 64; ++i) {
                if ((aperture >> (i % 16)) & 1)
                    held[i] = (word >> (8 * (i / 16))) & 255;
                if (held[i] >= READ(trial_out[i]))
                    comparisons[i / 16] |= 1u << (i % 16);
            }
#ifdef VERILATOR
            for (unsigned i = 0; i < 4; ++i) dut.comparisons_in[i] = comparisons[i];
            dut.eval();
#else
            ++_system_clock;
#endif
        }
        const unsigned output = READ(receive_out), output_valid = READ(receive_valid_out),
                       lock = READ(locked_out), quality = READ(quality_out), pairs = READ(pairs_locked_out),
                       map = READ(pair_mapping_out), taps = READ(pair_delays_out), busy = READ(acquiring_out);
        if (!reset) {
            trace << test << ',' << events << ',' << adc << ',' << pcs << ','
                  << (output_valid ? output : 0) << ',' << output_valid << ',' << lock << ',' << quality << ','
                  << pairs << ',' << (pairs ? map : 0) << ',' << (pairs ? taps : 0) << ',' << busy << ','
                  << READ(fault_out) << '\n';
            require(READ(fault_out) == unsigned(transport_fault), "unexpected asynchronous receive fault at event " + std::to_string(events) + " tag " + std::to_string(tag));
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
#ifdef QUALIFIED_ADC_PHY
        dut.ref_clk = ref;
#endif
        dut.adc_clk = adc; dut.pcs_clk = pcs; dut.eval();
#else
#ifdef QUALIFIED_ADC_PHY
        if (ref) dut._work_ref_clk(reset);
#endif
        if (adc) dut._work_adc_clk(reset);
        if (pcs) dut._work_pcs_clk(reset);
#ifdef QUALIFIED_ADC_PHY
        // Hold the asynchronous child reset through every coincident edge.
        if (withdrawn) {
            if (ref) dut.sim_assert_ref_reset();
            if (adc) dut.sim_assert_adc_reset();
            if (pcs) dut.sim_assert_pcs_reset();
        }
        if (ref) dut._strobe_ref_clk();
#endif
        if (adc) dut._strobe_adc_clk();
        if (pcs) dut._strobe_pcs_clk();
#ifdef QUALIFIED_ADC_PHY
        ++_system_clock;
#endif
#endif
#ifdef QUALIFIED_ADC_PHY
        was_withdrawn = withdrawn;
#endif
        ++events;
    };
    edge(true, true, true);
#ifdef QUALIFIED_ADC_PHY
    // One live clock cannot release the complete receive path. A power-good
    // pulse must be remembered even when neither delivered clock has an edge.
    for (unsigned i = 0; i < 16; ++i) edge(true, false, false);
    require(READ(reset_adc_out) && READ(reset_pcs_out), "ADC escaped before PCS reset acknowledgement");
    for (unsigned i = 0; i < 16; ++i) edge(false, true, false);
    require(READ(reset_adc_out), "stopped ADC domain released asynchronously");
    for (unsigned i = 0; i < 8; ++i) edge(true, true, false);
    require(!READ(reset_adc_out) && !READ(reset_pcs_out), "reset acknowledgement deadlock");
    power_good = false;
    edge(false, false, false, true);
    require(READ(reset_adc_out) && READ(reset_pcs_out), "missed stopped-clock power failure");
    for (unsigned i = 0; i < 64; ++i)
        require(!READ(acquire_out[i]) && !READ(convert_out[i]), "analog control escaped reset");
    power_good = true;
    edge(false, false, false, true);
    require(READ(reset_adc_out) && READ(reset_pcs_out), "reset pulse lost before clocks restarted");
    for (unsigned i = 0; i < 16; ++i) edge(true, false, false);
    require(READ(reset_adc_out), "restarted ADC ignored stopped PCS");
    for (unsigned i = 0; i < 16; ++i) edge(false, true, false);
    for (unsigned i = 0; i < 8; ++i) edge(true, true, false);
    require(!READ(reset_adc_out) && !READ(reset_pcs_out), "restart acknowledgement deadlock");
    edge(true, true, true);
#endif
    // Real precharge/release and SAR acquisition precede the first fixture
    // aperture; no initialized conversion result is injected into the PHY.
    for (unsigned warmup = 0; warmup <
#ifdef QUALIFIED_ADC_PHY
            4107;
#else
            4103;
#endif
            ++warmup)
        edge(true, true, false);
    unsigned total_frames = 0, total_bytes = 0;
    const unsigned epochs =
#ifdef QUALIFIED_ADC_PHY
        2;
#else
        1;
#endif
    for (unsigned epoch = 0; epoch < epochs; ++epoch) {
#ifdef QUALIFIED_ADC_PHY
        if (epoch) {
            power_good = false;
            edge(false, false, false, true);
            require(READ(reset_adc_out) && READ(reset_pcs_out), "active receiver missed stopped-clock reset");
            power_good = true;
            edge(false, false, false, true);
            require(READ(reset_adc_out) && READ(reset_pcs_out), "active receiver lost short reset request");
            // Reset only the scoreboard and external stimulus. DUT state and
            // held comparator samples persist until the real restart sequence.
            acquired = false; qualified = false; completed = 0; bytes = 0;
            word = 0x80808080; local = 1170; tag = 65520; valid = false; sampling = true;
            for (unsigned warmup = 0; warmup < 4107; ++warmup)
                edge(true, true, false);
        }
#endif
        unsigned next_adc = 0, next_pcs = test * 751, index = 0;
        const unsigned pcs_period = test % 2 ? 6944 : 7576;
        while (std::min(next_adc, next_pcs) < (2 * count + 64) * 4000) {
            const unsigned time = std::min(next_adc, next_pcs);
            const bool adc = time == next_adc, pcs = time == next_pcs;
            if (adc) {
                valid = index < 2 * count;
                sampling = index < 2 * count;
                tag = (65520 + index) & 65535;
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
                }
                ++index; next_adc += 4000;
            }
            if (pcs) next_pcs += pcs_period;
            edge(adc, pcs, false);
        }
        require(acquired && qualified && frame.empty() && completed == frame_count && bytes == 1598,
                "missing asynchronous Ethernet frame coverage");
        total_frames += completed;
        total_bytes += bytes;
    }
    signal = test != 0; ready = test != 1; transport_fault = test == 2; restart = test == 3;
    edge(true, true, false, true);
    std::cout << "case " << test << " master=" << master << " frames=" << total_frames << " bytes=" << total_bytes
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
