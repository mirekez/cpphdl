#ifdef VERILATOR
#include "VADCReceiveLaneController.h"
#else
#include "../adc_receive_lane.hpp"
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef VERILATOR
long _system_clock = -1;
#define VALUE(name) dut.name()
#else
#define VALUE(name) dut.name
#endif

static unsigned code(unsigned launch) {
    return ((launch * 73) ^ (launch >> 3) ^ 0x59) & 255;
}

int main(int argc, char** argv) {
    try {
        if (argc > 2)
            throw std::runtime_error("expected optional lane trace path");
#ifdef VERILATOR
        VADCReceiveLaneController dut;
#else
        ADCReceiveLaneController<13> dut;
#endif
        bool enabled = false, sampling = false;
        unsigned comparisons = 0;
        std::array<unsigned, 13> held{}, trials{};
#ifndef VERILATOR
        dut.enable_in = _ASSIGN(enabled);
        dut.sample_enable_in = _ASSIGN(sampling);
        dut.comparisons_in = _ASSIGN(comparisons);
        dut.offset_in = _ASSIGN(112);
        dut.gain_in = _ASSIGN(424);
        dut._assign();
#endif
        std::ofstream trace;
        if (argc == 2) {
            trace.open(argv[1]);
            if (!trace)
                throw std::runtime_error("cannot open lane trace");
        }
        auto cycle = [&](bool reset) {
            // Drive and observe are distinct cache epochs, equivalent to the
            // low/high phases supplied to Verilator. Work/strobe happen once.
            for (unsigned phase = 0; phase < 2; ++phase) {
#ifdef VERILATOR
                dut.enable_in = enabled;
                dut.sample_enable_in = sampling;
                dut.comparisons_in = comparisons;
                dut.offset_in = 112;
                dut.gain_in = 424;
                dut.reset = reset;
                dut.clk = phase == 0;
                dut.eval();
#else
                ++_system_clock;
                if (phase == 0) {
                    dut._work(reset);
                    dut._strobe();
                }
#endif
            }
        };
        unsigned raw_count = 0, normalized_count = 0;
        for (unsigned profile = 0; profile < 3; ++profile) {
            enabled = false;
            sampling = false;
            for (unsigned clock = 0; clock < 4; ++clock)
                cycle(true);
            trials.fill(0);
            held.fill(0);
            enabled = true;
            const bool gapped = profile == 1;
            auto launched = [&](unsigned clock) {
                return clock < 1024 && (!gapped || (clock % 11 != 3 && clock % 29 > 1));
            };
            for (unsigned age = 1; age <= 4100 + 1024 + 32; ++age) {
                const bool active = age >= 4101;
                const unsigned clock = active ? age - 4101 : 0;
                sampling = !active || launched(clock);
                if (active && sampling)
                    held[clock % 13] = code(clock);
                comparisons = 0;
                for (unsigned slice = 0; slice < 13; ++slice)
                    if (held[slice] >= trials[slice])
                        comparisons |= 1u << slice;
                cycle(false);
                const bool precharge = age < 4096, ready = age >= 4100;
                const bool valid = active && clock >= 13 && launched(clock - 13);
                const bool normalized_valid = active && clock >= 21 && launched(clock - 21);
                if (bool(VALUE(precharge_out)) != precharge || bool(VALUE(clear_out)) != !ready ||
                    bool(VALUE(startup_ready_out)) != ready || bool(VALUE(evaluate_enable_out)) != ready ||
                    bool(VALUE(valid_out)) != valid || bool(VALUE(normalized_valid_out)) != normalized_valid || VALUE(fault_out))
                    throw std::runtime_error("lane startup, comparator gate or conversion cadence failure");
                if (valid) {
                    const unsigned launch = clock - 13;
                    if (unsigned(VALUE(data_out)) != code(launch) || unsigned(VALUE(slice_out)) != launch % 13)
                        throw std::runtime_error("lane raw result or slice mismatch");
                    ++raw_count;
                }
                if (normalized_valid) {
                    const unsigned launch = clock - 21;
                    const int product = (int(code(launch)) - 112) * 424;
                    const int rounded = product < 0 ? -((-product + 128) / 256) : (product + 128) / 256;
                    const int value = 128 + rounded;
                    const unsigned expected = value < 0 ? 0 : value > 255 ? 255 : unsigned(value);
                    if (unsigned(VALUE(normalized_out)) != expected || unsigned(VALUE(normalized_slice_out)) != launch % 13 ||
                        bool(VALUE(clipped_out)) != (value < 0 || value > 255))
                        throw std::runtime_error("lane normalized result, clipping or tag mismatch");
                    ++normalized_count;
                }
                unsigned acquire = 0, convert = 0;
                for (unsigned slice = 0; slice < 16; ++slice) {
                    bool want_acquire = slice < 13 && precharge, want_convert = false;
                    for (unsigned delta = 1; slice < 13 && active && delta <= 11; ++delta)
                        if (clock >= delta && launched(clock - delta) && (clock - delta) % 13 == slice) {
                            want_acquire = want_acquire || delta <= 2;
                            want_convert = want_convert || delta >= 4;
                        }
                    if (bool(VALUE(acquire_out[slice])) != want_acquire || bool(VALUE(convert_out[slice])) != want_convert)
                        throw std::runtime_error("lane precharge/open/acquisition mask failure");
                    acquire |= unsigned(VALUE(acquire_out[slice])) << slice;
                    convert |= unsigned(VALUE(convert_out[slice])) << slice;
                    if (slice < 13)
                        trials[slice] = unsigned(VALUE(trial_out[slice]));
                    else if (unsigned(VALUE(trial_out[slice])) != 0)
                        throw std::runtime_error("lane padded slice is active");
                }
                if (trace) {
                    trace << profile << ',' << age << ',' << precharge << ',' << ready << ',' << acquire << ',' << convert
                          << ',' << unsigned(VALUE(data_out)) << ',' << unsigned(VALUE(slice_out)) << ',' << bool(VALUE(valid_out))
                          << ',' << unsigned(VALUE(normalized_out)) << ',' << bool(VALUE(normalized_valid_out)) << ','
                          << unsigned(VALUE(normalized_slice_out)) << ',' << bool(VALUE(clipped_out))
                          << ',' << bool(VALUE(clear_out)) << ',' << bool(VALUE(evaluate_enable_out)) << ',' << bool(VALUE(fault_out));
                    for (unsigned i = 0; i < 16; ++i)
                        trace << ',' << unsigned(VALUE(trial_out[i]));
                    trace << '\n';
                }
            }
            sampling = true;
            for (unsigned clock = 0; clock < 7; ++clock)
                cycle(false);
            // Dropping power-good restarts the full capacitor charge sequence.
            enabled = false;
            cycle(false);
            if (VALUE(valid_out) || VALUE(normalized_valid_out) || VALUE(startup_ready_out) || !VALUE(clear_out))
                throw std::runtime_error("lane disable did not clear readiness and data");
            for (unsigned i = 0; i < 16; ++i)
                if (VALUE(acquire_out[i]) || VALUE(convert_out[i]))
                    throw std::runtime_error("disabled lane still drives acquisition or conversion");
        }
        if (raw_count != normalized_count || raw_count < 2800)
            throw std::runtime_error("lane sample coverage failure");
        if (argc == 2) {
            trace.flush();
            if (!trace)
                throw std::runtime_error("lane trace write failure");
        }
        std::cout << "PASS initialized lane: " << raw_count << " raw/normalized samples, precharge/open/ready, gaps and restart\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
