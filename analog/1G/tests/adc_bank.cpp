#ifdef VERILATOR
#include "VInterleavedSARBankController.h"
#else
#include "../adc_interleaved_bank.hpp"
#endif
#include <fstream>
#include <array>
#include <iostream>
#include <stdexcept>

#ifndef VERILATOR
long _system_clock = -1;
#define VALUE(name) dut.name()
#else
#define VALUE(name) dut.name
#endif

static void advance_clock() {
#ifndef VERILATOR
    ++_system_clock;
#endif
}

static unsigned target(unsigned launch) {
    return ((launch * 73) ^ (launch >> 3) ^ 0x59) & 255;
}

template <unsigned Count>
static void check_bank(std::ostream* trace = nullptr, bool gapped = false) {
#ifdef VERILATOR
    static_assert(Count == BANK_SLICES);
    VInterleavedSARBankController dut;
#else
    InterleavedSARBankController<Count> dut;
#endif
    bool enabled = false, sampling = false;
    unsigned comparisons = 0;
    std::array<unsigned, Count> held{};
    #ifndef VERILATOR
    dut.enable_in = _ASSIGN(enabled);
    dut.sample_enable_in = _ASSIGN(sampling);
    dut.comparisons_in = _ASSIGN(comparisons);
    dut._assign();
    #endif
    auto cycle = [&](bool resetting) {
#ifdef VERILATOR
        dut.enable_in = enabled;
        dut.sample_enable_in = sampling;
        dut.comparisons_in = comparisons;
        dut.reset = resetting;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
#else
        dut._work(resetting);
        dut._strobe();
#endif
    };
    for (unsigned reset_clock = 0; reset_clock < 4; ++reset_clock) {
        advance_clock();
        cycle(true);
    }
    constexpr unsigned launches = 4096;
    auto launched = [&](unsigned clock) {
        return clock < launches && (!gapped || (clock % 11 != 3 && clock % 29 > 1));
    };
    unsigned received = 0, expected_count = 0;
    bool collided = false;
    enabled = true;
    for (unsigned clock = 0; clock < launches + 32; ++clock) {
        advance_clock();
        sampling = launched(clock);
        expected_count += unsigned(sampling);
        if (sampling)
            held[clock % Count] = target(clock);
        comparisons = 0;
        for (unsigned slice = 0; slice < Count; ++slice)
            if (held[slice] >= unsigned(VALUE(trial_out[slice])))
                comparisons |= 1u << slice;
        cycle(false);
        collided = collided || VALUE(fault_out);
        if constexpr (Count >= 13) {
            const bool valid = clock >= 13 && launched(clock - 13);
            if (bool(VALUE(valid_out)) != valid || VALUE(fault_out))
                throw std::runtime_error("SAR bank cadence or collision failure");
            if (valid) {
                const unsigned launch = clock - 13;
                if (unsigned(VALUE(data_out)) != target(launch) || unsigned(VALUE(slice_out)) != launch % Count)
                    throw std::runtime_error("SAR bank data or slice identity failure");
                ++received;
            }
            for (unsigned slice = 0; slice < Count; ++slice) {
                bool acquire = false, convert = false;
                for (unsigned delta = 1; delta <= 11; ++delta)
                    if (clock >= delta && launched(clock - delta) && (clock - delta) % Count == slice) {
                        acquire = acquire || delta <= 2;
                        convert = convert || delta >= 4;
                    }
                if (bool(VALUE(acquire_out[slice])) != acquire || bool(VALUE(convert_out[slice])) != convert)
                    throw std::runtime_error("SAR bank acquisition or conversion timing failure");
            }
        }
        if (trace) {
            unsigned acquire = 0, convert = 0;
            for (unsigned slice = 0; slice < Count; ++slice) {
                acquire |= unsigned(VALUE(acquire_out[slice])) << slice;
                convert |= unsigned(VALUE(convert_out[slice])) << slice;
            }
            *trace << gapped << ',' << clock << ',' << unsigned(VALUE(data_out)) << ',' << unsigned(VALUE(slice_out))
                   << ',' << bool(VALUE(valid_out)) << ',' << bool(VALUE(fault_out))
                   << ',' << acquire << ',' << convert << '\n';
        }
    }
    if constexpr (Count >= 13) {
        if (received != expected_count || collided)
            throw std::runtime_error("SAR bank lost samples");
    } else if (!collided) {
        throw std::runtime_error("unsafe SAR bank did not report a collision");
    }
    std::cout << "PASS " << Count << " slices: " << received << " samples, gaps " << gapped << ", collision " << collided << '\n';
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
#ifndef VERILATOR
            check_bank<12>();
            check_bank<13>();
            check_bank<16>();
            check_bank<13>(nullptr, true);
            check_bank<16>(nullptr, true);
#else
            check_bank<BANK_SLICES>();
            check_bank<BANK_SLICES>(nullptr, true);
#endif
        } else if (argc == 3) {
            const unsigned count = std::stoul(argv[1]);
            std::ofstream trace(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open SAR bank trace");
#ifdef VERILATOR
            if (count != BANK_SLICES)
                throw std::runtime_error("wrong compiled RTL SAR bank size");
            check_bank<BANK_SLICES>(&trace);
            check_bank<BANK_SLICES>(&trace, true);
#else
            if (count == 12) {
                check_bank<12>(&trace);
                check_bank<12>(&trace, true);
            } else if (count == 13) {
                check_bank<13>(&trace);
                check_bank<13>(&trace, true);
            } else if (count == 16) {
                check_bank<16>(&trace);
                check_bank<16>(&trace, true);
            }
            else
                throw std::runtime_error("unsupported SAR bank test size");
#endif
            trace.flush();
            if (!trace)
                throw std::runtime_error("SAR bank trace write failed");
        } else {
            throw std::runtime_error("expected optional slice count and trace path");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
