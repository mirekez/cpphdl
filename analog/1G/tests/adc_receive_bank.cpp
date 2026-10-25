#ifdef VERILATOR
#include "VADCReceiveBankController.h"
#else
#include "../adc_receive_bank.hpp"
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

static unsigned target(unsigned launch) {
    return ((launch * 73) ^ (launch >> 3) ^ 0x59) & 255;
}

static unsigned offset_at(unsigned clock, bool variable) {
    return variable ? (clock * 17 + 31) & 255 : 112;
}

static unsigned gain_at(unsigned clock, bool variable) {
    constexpr std::array<unsigned, 8> gains{0, 1, 128, 256, 424, 1024, 32768, 65535};
    return variable ? gains[clock % gains.size()] : 424;
}

static int normalized(unsigned sample, unsigned offset, unsigned gain) {
    const int product = (int(sample) - int(offset)) * int(gain);
    const int rounded = product < 0 ? -((-product + 128) / 256) : (product + 128) / 256;
    return 128 + rounded;
}

static void check_receiver(std::ostream* trace = nullptr) {
#ifdef VERILATOR
    VADCReceiveBankController dut;
#else
    ADCReceiveBankController<13> dut;
#endif
    bool enabled = false, sampling = false, precharging = false;
    unsigned comparisons = 0, offset = 112, gain = 424;
    std::array<unsigned, 13> held{};
    std::array<unsigned, 16> trials{};
#ifndef VERILATOR
    dut.precharge_in = _ASSIGN(precharging);
    dut.enable_in = _ASSIGN(enabled);
    dut.sample_enable_in = _ASSIGN(sampling);
    dut.comparisons_in = _ASSIGN(comparisons);
    dut.offset_in = _ASSIGN(offset);
    dut.gain_in = _ASSIGN(gain);
    dut._assign();
#endif
    auto advance = [&]() {
#ifndef VERILATOR
        ++_system_clock;
#endif
    };
    auto cycle = [&](bool reset) {
#ifdef VERILATOR
        dut.precharge_in = precharging;
        dut.enable_in = enabled;
        dut.sample_enable_in = sampling;
        dut.comparisons_in = comparisons;
        dut.offset_in = offset;
        dut.gain_in = gain;
        dut.reset = reset;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
    };
    constexpr unsigned launches = 2048;
    unsigned accepted = 0, raw_results = 0, normalized_results = 0, clipped_results = 0;
    for (unsigned profile = 0; profile < 4; ++profile) {
        enabled = false;
        sampling = false;
        for (unsigned clock = 0; clock < 4; ++clock) {
            advance();
            cycle(true);
            if (VALUE(valid_out) || VALUE(normalized_valid_out) || VALUE(fault_out))
                throw std::runtime_error("receive bank did not reset");
        }
        enabled = true;
        precharging = true;
        sampling = true;
        for (unsigned clock = 0; clock < 8; ++clock) {
            advance();
            cycle(false);
            if (VALUE(valid_out) || VALUE(normalized_valid_out) || VALUE(fault_out))
                throw std::runtime_error("precharge admitted a conversion result");
            for (unsigned slice = 0; slice < 16; ++slice)
                if (bool(VALUE(acquire_out[slice])) != (slice < 13) || VALUE(convert_out[slice]))
                    throw std::runtime_error("precharge did not track exactly the physical slices");
        }
        precharging = false;
        enabled = false;
        advance();
        cycle(false);
        trials.fill(0);
        const bool variable = profile >= 2, gapped = (profile & 1) != 0;
        auto launched = [&](unsigned clock) {
            return clock < launches && (!gapped || (clock % 11 != 3 && clock % 29 > 1));
        };
        enabled = true;
        for (unsigned clock = 0; clock < launches + 32; ++clock) {
            advance();
            sampling = launched(clock);
            accepted += unsigned(sampling);
            offset = offset_at(clock, variable);
            gain = gain_at(clock, variable);
            if (sampling)
                held[clock % 13] = target(clock);
            // High unused comparison bits deliberately toggle. They must not
            // enter the thirteen-slice controller or create a phantom result.
            comparisons = ((clock & 7) << 13);
            for (unsigned slice = 0; slice < 13; ++slice)
                if (held[slice] >= trials[slice])
                    comparisons |= 1u << slice;
            cycle(false);
            const bool valid = clock >= 13 && launched(clock - 13);
            const bool normalized_valid = clock >= 21 && launched(clock - 21);
            if (bool(VALUE(valid_out)) != valid || bool(VALUE(normalized_valid_out)) != normalized_valid || VALUE(fault_out))
                throw std::runtime_error("receive bank lost cadence or raised a collision");
            if (valid) {
                const unsigned launch = clock - 13;
                if (unsigned(VALUE(data_out)) != target(launch) || unsigned(VALUE(slice_out)) != launch % 13)
                    throw std::runtime_error("receive bank raw data or tag mismatch");
                ++raw_results;
            }
            if (normalized_valid) {
                const unsigned launch = clock - 21;
                // The normalizer samples the parent's raw result at the next
                // clock. Its calibration must travel with that same sample.
                const int value = normalized(target(launch), offset_at(launch + 14, variable), gain_at(launch + 14, variable));
                const unsigned expected = value < 0 ? 0 : value > 255 ? 255 : unsigned(value);
                const bool clipped = value < 0 || value > 255;
                if (unsigned(VALUE(normalized_out)) != expected || bool(VALUE(clipped_out)) != clipped ||
                    unsigned(VALUE(normalized_slice_out)) != launch % 13)
                    throw std::runtime_error("receive bank calibration, clipping or normalized tag mismatch");
                ++normalized_results;
                clipped_results += unsigned(clipped);
            }
            for (unsigned slice = 0; slice < 16; ++slice) {
                // Save the achieved trial for the next comparator interval.
                // Reading it before this cycle would cache a pre-edge value
                // when the same combinational port is observed after strobe.
                trials[slice] = unsigned(VALUE(trial_out[slice]));
                bool acquire = false, convert = false;
                for (unsigned delta = 1; slice < 13 && delta <= 11; ++delta)
                    if (clock >= delta && launched(clock - delta) && (clock - delta) % 13 == slice) {
                        acquire = acquire || delta <= 2;
                        convert = convert || delta >= 4;
                    }
                if (bool(VALUE(acquire_out[slice])) != acquire || bool(VALUE(convert_out[slice])) != convert ||
                    (slice >= 13 && unsigned(VALUE(trial_out[slice])) != 0))
                    throw std::runtime_error("receive bank physical control or unused position mismatch");
            }
            if (trace) {
                *trace << profile << ',' << clock << ',' << unsigned(VALUE(data_out)) << ',' << unsigned(VALUE(slice_out))
                       << ',' << bool(VALUE(valid_out)) << ',' << bool(VALUE(fault_out))
                       << ',' << unsigned(VALUE(normalized_out)) << ',' << bool(VALUE(normalized_valid_out))
                       << ',' << bool(VALUE(clipped_out)) << ',' << unsigned(VALUE(normalized_slice_out));
                for (unsigned slice = 0; slice < 16; ++slice)
                    *trace << ',' << unsigned(VALUE(trial_out[slice])) << ',' << bool(VALUE(acquire_out[slice]))
                           << ',' << bool(VALUE(convert_out[slice]));
                *trace << '\n';
            }
        }
        // Disable while work is in flight, then check that no queued result
        // survives the documented pipeline flush and restart boundary.
        sampling = true;
        for (unsigned clock = 0; clock < 7; ++clock) {
            advance();
            cycle(false);
        }
        precharging = true;
        for (unsigned clock = 0; clock < 12; ++clock) {
            advance();
            cycle(false);
            if (VALUE(valid_out) || VALUE(normalized_valid_out) || VALUE(fault_out))
                throw std::runtime_error("precharge did not flush in-flight conversions");
        }
        precharging = false;
        enabled = false;
        for (unsigned clock = 0; clock < 24; ++clock) {
            advance();
            cycle(false);
            if (VALUE(valid_out) || VALUE(normalized_valid_out) || VALUE(fault_out))
                throw std::runtime_error("disabled receive bank leaked a queued result");
        }
    }
    if (accepted != raw_results || accepted != normalized_results || !clipped_results)
        throw std::runtime_error("receive bank coverage or sample count mismatch");
    std::cout << "PASS receive bank: " << accepted << " samples, " << clipped_results
              << " clipped, continuous/gapped sampling, changing calibration and disable flush\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 1)
            check_receiver();
        else if (argc == 2) {
            std::ofstream trace(argv[1]);
            if (!trace)
                throw std::runtime_error("cannot open receive bank trace");
            check_receiver(&trace);
            trace.flush();
            if (!trace)
                throw std::runtime_error("receive bank trace write failed");
        } else
            throw std::runtime_error("expected optional receive bank trace path");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
