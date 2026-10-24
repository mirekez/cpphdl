#include "../lead_receive_common_reference.hpp"
#include "../../biased_mdi_input_network.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected destination run|export");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        if (mode != "run" && mode != "export")
            throw std::runtime_error("unknown shared common-mode experiment mode");
        constexpr double warmup = 16e-6, stop = warmup + 256e-9, step = 2e-12;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        electrical ground, supply, rail;
        sim.reference(ground);
        supply = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 3.3}});
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12));
        one_g::ihp_sg13g2::lead_receive_common_reference reference;
        reference.OUT = capacitor(ground, capacitance_value(16e-12));
        reference.Vdd = rail;
        reference.Vss = ground;
        reference.elaborate();
        std::array<one_g::biased_mdi_input_network, 8> lanes;
        for (unsigned lane = 0; lane < lanes.size(); ++lane) {
            std::vector<std::pair<double, double>> plus{{0, 0}, {1e-6, 0}, {1.1e-6, 2.3}}, minus = plus;
            for (unsigned symbol = 0; symbol < 32; ++symbol) {
                const double begin = warmup + symbol * 8e-9;
                const double mean = symbol % 3 == 0 ? 2.1 : symbol % 3 == 1 ? 2.3 : 2.5;
                const double half = .25 * (int((symbol * 3 + lane) % 5) - 2);
                plus.emplace_back(begin, plus.back().second);
                minus.emplace_back(begin, minus.back().second);
                plus.emplace_back(begin + .1e-9, mean + half);
                minus.emplace_back(begin + .1e-9, mean - half);
            }
            plus.emplace_back(stop, plus.back().second);
            minus.emplace_back(stop, minus.back().second);
            electrical p, n;
            p = sim_pwl_voltage_source(ground, plus);
            n = sim_pwl_voltage_source(ground, minus);
            auto& input = lanes[lane];
            input.P = resistor(p, resistance(25));
            input.N = resistor(n, resistance(25));
            input.VCM = reference.OUT;
            input.Vdd = rail;
            input.Vss = ground;
            input.elaborate();
            input.PLUS = resistor(input.MINUS, resistance(100));
            input.PLUS = capacitor(ground, capacitance_value(150e-15));
            input.MINUS = capacitor(ground, capacitance_value(150e-15));
        }
        sim.observe("OUT", reference.OUT, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("TARGET", reference.target.TAP, ground);
        sim.observe("DRIVE", reference.buffer.DRIVE, ground);
        sim.observe("BIAS_N", reference.n_bias.TAP, ground);
        sim.observe("BIAS_P", reference.p_bias.TAP, ground);
        sim.observe("TAIL_BIAS", reference.TAIL_BIAS, ground);
        for (unsigned lane = 0; lane < lanes.size(); ++lane) {
            const auto suffix = std::to_string(lane);
            sim.observe("P" + suffix, lanes[lane].P, ground);
            sim.observe("N" + suffix, lanes[lane].N, ground);
            sim.observe("PLUS" + suffix, lanes[lane].PLUS, ground);
            sim.observe("MINUS" + suffix, lanes[lane].MINUS, ground);
        }
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(step), duration(step), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup), duration(1e-9));
            sim.discard_trace();
            // Keep each advance below the solver's regular-step limit.
            sim.advance_to(duration(warmup + 128e-9), duration(step));
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
