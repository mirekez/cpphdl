#pragma once
#include <sparse.hpp>
#include <VerilogA/experiment.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace analog;

template <class Receiver> int run_programmable_array(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("expected destination run|export gain_code [uniform]");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned code = std::stoul(argv[3]);
        if ((mode != "run" && mode != "export") || code < 4 || code > 8)
            throw std::runtime_error("invalid programmable receiver experiment");
        if (argc == 5 && (mode != "run" || std::string(argv[4]) != "uniform"))
            throw std::runtime_error("uniform is a native integration profile");
        const bool uniform = argc == 5;
        constexpr double warmup = 16e-6, stop = warmup + 256e-9;
        constexpr double export_step = 20e-12;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        electrical ground, supply, rail, logic_source, logic_rail, plus, minus;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12), voltage(3.3));
        logic_source = sim_voltage_source(ground, voltage(1.2));
        logic_rail = resistor(logic_source, resistance(10));
        logic_rail = capacitor(ground, capacitance_value(20e-12));
        using wave = std::vector<std::pair<double, double>>;
        wave positive{{0, 0}, {1e-6, 0}, {1.1e-6, 1.65}};
        wave negative = positive;
        std::array<wave, 16> tracks;
        for (auto& points : tracks)
            points = {{0, 0}};
        const std::array<int, 16> symbols{0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0};
        const double amplitude = .12 * (10 - code) / code;
        for (unsigned symbol = 0; symbol < 32; ++symbol) {
            const double begin = warmup + symbol * 8e-9;
            positive.emplace_back(begin, positive.back().second);
            positive.emplace_back(begin + .1e-9, 1.65 + amplitude * symbols[symbol % symbols.size()] / 2);
            negative.emplace_back(begin, negative.back().second);
            negative.emplace_back(begin + .1e-9, 1.65 - amplitude * symbols[symbol % symbols.size()] / 2);
        }
        positive.emplace_back(stop, positive.back().second);
        negative.emplace_back(stop, negative.back().second);
        // One acquisition every 4 ns across sixteen physical samplers.
        // Alternating phases provide two timing observations per PAM symbol.
        for (unsigned sample = 0; sample < 64; ++sample) {
            auto& points = tracks[sample % tracks.size()];
            const double begin = warmup + sample * 4e-9;
            points.emplace_back(begin, 0);
            points.emplace_back(begin + .1e-9, 3.3);
            points.emplace_back(begin + 2.5e-9, 3.3);
            points.emplace_back(begin + 2.6e-9, 0);
        }
        for (auto& points : tracks)
            points.emplace_back(stop, 0);
        plus = sim_pwl_voltage_source(ground, positive);
        minus = sim_pwl_voltage_source(ground, negative);
        Receiver receiver;
        receiver.PLUS = resistor(plus, resistance(50));
        receiver.MINUS = resistor(minus, resistance(50));
        receiver.Vdd = rail;
        receiver.Vss = ground;
        receiver.Vlogic = logic_rail;
        receiver.EVALUATE = ground;
        receiver.CLEAR = rail;
        for (unsigned slice = 0; slice < 16; ++slice) {
            electrical track_source;
            track_source = sim_pwl_voltage_source(ground, tracks[slice]);
            receiver.TRACK[slice] = resistor(track_source, resistance(50));
            for (unsigned bit = 0; bit < 8; ++bit)
                receiver.BIT_N[slice][bit] = rail;
            receiver.CORE_DECISION[slice] = capacitor(ground, capacitance_value(20e-15));
        }
        for (unsigned bit = 0; bit < 5; ++bit) {
            electrical control;
            control = sim_voltage_source(ground, voltage(bit == code - 4 ? 0 : 3.3));
            receiver.SELECT_N[bit] = resistor(control, resistance(50));
        }
        receiver.elaborate();
        sim.observe("PLUS", receiver.PLUS, ground);
        sim.observe("MINUS", receiver.MINUS, ground);
        sim.observe("GAIN_INPUT", receiver.gain.IN, ground);
        sim.observe("GAIN_OUTPUT", receiver.gain.OUT, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("VLOGIC", logic_rail, ground);
        sim.observe("GAIN_BIAS", receiver.gain.BIAS, ground);
        for (unsigned slice = 0; slice < 16; ++slice) {
            sim.observe("HELD" + std::to_string(slice), receiver.adc.slices[slice].sampler.OUT, ground);
            sim.observe("TRACK" + std::to_string(slice), receiver.TRACK[slice], ground);
        }
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(export_step), duration(export_step), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup), duration(1e-9));
            sim.discard_trace();
            if (uniform) {
                sim.advance_to(duration(warmup + 128e-9), duration(2e-12));
                sim.advance_to(duration(stop), duration(2e-12));
            } else {
                // Resolve each physical clock ramp and its charge transient at
                // 2 ps. Quiet intervals use 20 ps in the same continuous state.
                for (unsigned sample = 0; sample < 64; ++sample) {
                    const double begin = warmup + sample * 4e-9;
                    sim.advance_to(duration(begin + .5e-9), duration(2e-12));
                    sim.advance_to(duration(begin + 2.5e-9), duration(20e-12));
                    sim.advance_to(duration(begin + 3e-9), duration(2e-12));
                    sim.advance_to(duration(warmup + (sample + 1) * 4e-9), duration(20e-12));
                }
            }
            std::ofstream schedule(destination / "native-schedule.json");
            schedule << "{\"warmup_step_s\":1e-9,\"measurement_profile\":\""
                     << (uniform ? "uniform_2ps" : "2ps_clock_windows_20ps_quiet")
                     << "\",\"rising_fine_window_ns\":[0,0.5],\"falling_fine_window_ns\":[2.5,3],"
                     << "\"sample_period_ns\":4}\n";
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
