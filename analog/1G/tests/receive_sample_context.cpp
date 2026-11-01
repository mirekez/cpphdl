#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef VERILATOR
#include "VReceiveSampleContext.h"
using DUT = VReceiveSampleContext;
#else
#include "../receive_sample_context.hpp"
using DUT = ReceiveSampleContext;
long _system_clock = -1;
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Sample {
    unsigned tag = 0, local = 0, desired = 0;
    bool train = false, clipped = false, present = false;
};
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected trace destination");
        std::ofstream trace(argv[1]);
        require(bool(trace), "cannot open trace");
        DUT dut;
        std::array<Sample, 16> history{};
        unsigned tag = 0, local = 0, desired = 0, center = 0, fraction = 0, latest = 0, cycle = 0, checked = 0;
        bool train = false, clipped = false, accept = false, query = false, sticky = false;
#ifndef VERILATOR
        dut.sample_tag_in = _ASSIGN(tag);
        dut.local_in = _ASSIGN(local);
        dut.desired_in = _ASSIGN(desired);
        dut.train_in = _ASSIGN(train);
        dut.clipped_in = _ASSIGN(clipped);
        dut.sample_valid_in = _ASSIGN(accept);
        dut.center_tag_in = _ASSIGN(center);
        dut.center_fraction_in = _ASSIGN(fraction);
        dut.center_valid_in = _ASSIGN(query);
        dut._assign();
#endif
        auto edge = [&](bool reset) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.sample_tag_in = tag;
            dut.local_in = local;
            dut.desired_in = desired;
            dut.train_in = train;
            dut.clipped_in = clipped;
            dut.sample_valid_in = accept;
            dut.center_tag_in = center;
            dut.center_fraction_in = fraction;
            dut.center_valid_in = query;
            dut.eval();
#define READ(name) unsigned(dut.name)
#else
            ++_system_clock;
#define READ(name) unsigned(dut.name())
#endif
            const unsigned reference_tag = (center + (fraction >= 8388608)) & 65535;
            const Sample saved = history[reference_tag % 16];
            const unsigned age = (latest - center) & 65535;
            const bool expected_fault = sticky || (query && (!saved.present || saved.tag != reference_tag || age < 4 || age > 12));
            bool support_clip = false;
            for (unsigned i = 0; i < 8; ++i)
                support_clip |= history[(center + 65533 + i) % 16].clipped;
            const unsigned valid = READ(valid_out), fault = READ(fault_out), out_local = READ(local_out),
                           out_desired = READ(desired_out), out_train = READ(train_out), out_clip = READ(clipped_out);
            if (!reset) {
                require(fault == unsigned(expected_fault), "context fault mismatch at " + std::to_string(cycle));
                require(valid == unsigned(query && !expected_fault), "context validity mismatch");
                if (valid) {
                    ++checked;
                    require(out_local == saved.local && out_desired == saved.desired && out_train == unsigned(saved.train),
                            "metadata not aligned to the requested center tag");
                    require(out_clip == unsigned(support_clip), "clip missing from interpolation support");
                }
                trace << cycle << ',' << valid << ',' << fault << ',' << (valid ? out_local : 0) << ','
                      << (valid ? out_desired : 0) << ',' << (valid ? out_train : 0) << ',' << (valid ? out_clip : 0) << '\n';
            }
#undef READ
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(reset);
            dut._strobe();
#endif
            if (reset) {
                history = {};
                latest = 0;
                sticky = false;
            } else {
                if (accept && !sticky) {
                    history[tag % 16] = Sample{tag, local, desired, train, clipped, true};
                    latest = tag;
                }
                sticky = expected_fault;
            }
            ++cycle;
        };
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            query = accept = false;
            edge(true);
            for (unsigned n = 0; n < 70000; ++n) {
                tag = (65520 + n) & 65535;
                local = (n * 73 + scenario) & 4095;
                desired = (n * 211 + scenario * 17) & 4095;
                fraction = n % 4 == 0 ? 0 : n % 4 == 1 ? 8388607 : n % 4 == 2 ? 8388608 : 16777215;
                train = n % 97 < 31;
                clipped = n % 53 == 17;
                accept = true;
                query = n >= 16;
                center = (tag - (5 + n % 9)) & 65535; // Stored age 4..12, including wrap.
                edge(false);
                if (n % 29 == 0) {
                    accept = false;
                    local = desired = 4095;
                    center = (latest - 8) & 65535;
                    edge(false);
                }
            }
            accept = false;
            query = true;
            center = (latest - (scenario == 0 ? 3 : scenario == 1 ? 13 : scenario == 2 ? 16 : 65535)) & 65535;
            edge(false);
            center = (latest - 8) & 65535;
            for (unsigned i = 0; i < 8; ++i)
                edge(false); // A subsequent valid tag must not clear a fault.
        }
        require(checked > 280000, "insufficient valid metadata checks");
        std::cout << cycle << " context clocks PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
