#pragma once
#include <sparse.hpp>
#include <VerilogA/experiment.hpp>
#include <filesystem>
#include <iostream>
#include <string>

using namespace analog;

template <class Receiver> int run_programmable_receiver(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("expected destination run|export gain_code");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned code = std::stoul(argv[3]);
        if ((mode != "run" && mode != "export") || code < 4 || code > 8)
            throw std::runtime_error("invalid programmable receiver experiment");
        constexpr double warmup = 16e-6, stop = warmup + 128e-9;
        const double step = mode == "run" ? 2e-12 : 20e-12;
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
        wave negative = positive, track{{0, 0}};
        const std::array<int, 16> symbols{0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0};
        const double amplitude = .12 * (10 - code) / code;
        for (unsigned symbol = 0; symbol < symbols.size(); ++symbol) {
            const double begin = warmup + symbol * 8e-9;
            positive.emplace_back(begin, positive.back().second);
            positive.emplace_back(begin + .1e-9, 1.65 + amplitude * symbols[symbol] / 2);
            negative.emplace_back(begin, negative.back().second);
            negative.emplace_back(begin + .1e-9, 1.65 - amplitude * symbols[symbol] / 2);
            // A fixed late aperture includes propagation through both gain paths.
            track.emplace_back(begin + 4e-9, 0);
            track.emplace_back(begin + 4.1e-9, 3.3);
            track.emplace_back(begin + 6.5e-9, 3.3);
            track.emplace_back(begin + 6.6e-9, 0);
        }
        positive.emplace_back(stop, positive.back().second);
        negative.emplace_back(stop, negative.back().second);
        track.emplace_back(stop, 0);
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
        electrical track_source;
        track_source = sim_pwl_voltage_source(ground, track);
        receiver.TRACK[0] = resistor(track_source, resistance(50));
        for (unsigned bit = 0; bit < 8; ++bit)
            receiver.BIT_N[0][bit] = rail;
        for (unsigned bit = 0; bit < 5; ++bit) {
            electrical control;
            control = sim_voltage_source(ground, voltage(bit == code - 4 ? 0 : 3.3));
            receiver.SELECT_N[bit] = resistor(control, resistance(50));
        }
        receiver.CORE_DECISION[0] = capacitor(ground, capacitance_value(20e-15));
        receiver.elaborate();
        sim.observe("PLUS", receiver.PLUS, ground);
        sim.observe("MINUS", receiver.MINUS, ground);
        sim.observe("GAIN_INPUT", receiver.gain.IN, ground);
        sim.observe("GAIN_OUTPUT", receiver.gain.OUT, ground);
        sim.observe("HELD", receiver.adc.slices[0].sampler.OUT, ground);
        sim.observe("TRACK", receiver.TRACK[0], ground);
        sim.observe("VDD", rail, ground);
        sim.observe("VLOGIC", logic_rail, ground);
        sim.observe("GAIN_BIAS", receiver.gain.BIAS, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(step), duration(step), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup), duration(1e-9));
            sim.discard_trace();
            sim.advance_to(duration(stop), duration(step));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
