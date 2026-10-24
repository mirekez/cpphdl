#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VADCSARController.h"
using DUT = VADCSARController;
#else
#include "../adc_sar_control.hpp"
using DUT = ADCSARController;
long _system_clock = -1;
#endif

int main(int argc, char** argv) {
    try {
        if (argc > 2)
            throw std::runtime_error("expected [trace.csv]");
        std::ofstream trace;
        if (argc == 2)
            trace.open(argv[1]);
        DUT dut;
        bool start = false, comparison = false;
#ifndef VERILATOR
        dut.start_in = _ASSIGN(start);
        dut.comparison_in = _ASSIGN(comparison);
        dut._assign();
#endif
        // Independent interval bisection and an age-based control schedule.
        // The actual DUT trial drives feedback; truth is absent from the DUT.
        int age = -1, target = 0, low = 0, high = 256;
        unsigned expected_trial = 0, expected_data = 0, actual_trial = 0, cycles = 0, conversions = 0;
        auto step = [&](int input, bool launch, bool reset = false) {
            start = launch;
            if (age < 0 && start)
                target = input;
            comparison = target >= int(actual_trial);
            bool expected_acquire = false, expected_convert = false, expected_valid = false;
            if (reset) {
                age = -1;
                expected_trial = expected_data = 0;
            } else {
                if (age < 0 && start) {
                    age = 0;
                    low = 0;
                    high = 256;
                    expected_trial = 0;
                } else if (age >= 0) {
                    ++age;
                }
                expected_acquire = age == 1 || age == 2;
                expected_convert = age >= 4 && age < 12;
                if (age == 4)
                    expected_trial = 128;
                if (age >= 5 && age <= 12) {
                    int midpoint = (low + high) / 2;
                    if (target >= midpoint)
                        low = midpoint;
                    else
                        high = midpoint;
                    expected_trial = age < 12 ? unsigned((low + high) / 2) : unsigned(low);
                    if (age == 12) {
                        expected_data = unsigned(low);
                        expected_valid = true;
                        ++conversions;
                        if (expected_data != unsigned(std::clamp(target, 0, 255)))
                            throw std::runtime_error("independent SAR quantizer failed");
                        age = -1;
                    }
                }
            }
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.start_in = start;
            dut.comparison_in = comparison;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            actual_trial = dut.trial_out;
            unsigned data = dut.data_out;
            bool valid = dut.valid_out, acquire = dut.acquire_out, convert = dut.convert_out, busy = dut.busy_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            actual_trial = unsigned(dut.trial_out());
            unsigned data = unsigned(dut.data_out());
            bool valid = dut.valid_out(), acquire = dut.acquire_out(), convert = dut.convert_out(), busy = dut.busy_out();
#endif
            if (trace.is_open())
                trace << actual_trial << ',' << data << ',' << valid << ',' << acquire << ',' << convert << ',' << busy << '\n';
            if (actual_trial != expected_trial || data != expected_data || valid != expected_valid ||
                    acquire != expected_acquire || convert != expected_convert || busy != (age >= 0) ||
                    (acquire && convert))
                throw std::runtime_error("SAR control/bisection mismatch at cycle " + std::to_string(cycles));
            ++cycles;
            return valid;
        };
        step(0, false, true);
        for (int code = -4; code < 260; ++code) {
            step(code, true);
            for (unsigned tick = 0; tick < 12; ++tick)
                step(code + 100, tick % 3 == 0); // Busy launches cannot replace the captured transaction.
            step(0, false);
        }
        for (unsigned interrupted = 0; interrupted <= 12; ++interrupted) {
            step(211, true);
            for (unsigned tick = 0; tick < interrupted; ++tick)
                step(211, false);
            step(0, true, true);
            for (unsigned tick = 0; tick < 15; ++tick)
                step(0, false);
        }
        for (unsigned tick = 0; tick < 13 * 512; ++tick)
            step(int((tick / 13) * 37) % 256, true); // Held start requests continuous conversions.
        step(0, false, true);
        std::cout << cycles << " SAR phase/bisection states, " << conversions
                  << " conversions; all codes, saturation, busy starts and reset at every phase\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
