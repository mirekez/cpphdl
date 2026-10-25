#include "../mdi_input_network.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;
using namespace analog::one_g;

int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    try {
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const double step_ps = std::stod(argv[3]);
        if (step_ps != 20 && step_ps != 10)
            throw std::runtime_error("use a 10 or 20 ps integration step");
        const double step = step_ps * 1e-12;
        simulation sim;
        sim.analytic_jacobian = true;
        electrical ground, supply, rail, command_p, command_n;
        std::vector<std::pair<double, double>> positive{{0, 2.3}}, negative{{0, 2.3}};
        double previous_p = 2.3, previous_n = 2.3;
        for (unsigned i = 0; i < 629; ++i) {
            double common = 2.3, difference = 0;
            if (i < 625) {
                const unsigned a = i % 25, b = i / 25;
                const auto contribution = [](unsigned s) {
                    return -.125 * (3 * (int(s / 5) - 2) + int(s % 5) - 2);
                };
                difference = contribution(a) + contribution(b);
            } else {
                common = i == 625 || i == 627 ? 3.3 : 1.65;
            }
            const double time = 100e-9 + i * 8e-9;
            const double next_p = common + difference / 2, next_n = common - difference / 2;
            positive.emplace_back(time, previous_p);
            positive.emplace_back(time + .1e-9, next_p);
            negative.emplace_back(time, previous_n);
            negative.emplace_back(time + .1e-9, next_n);
            previous_p = next_p;
            previous_n = next_n;
        }
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(1));
        rail = capacitor(ground, capacitance_value(20e-12));
        command_p = sim_pwl_voltage_source(ground, positive);
        command_n = sim_pwl_voltage_source(ground, negative);
        mdi_input_network network;
        network.Vdd = rail;
        network.Vss = ground;
        // The two-driver Norton equivalent has 50 ohms differential source
        // impedance. This passive fixture tests conditioning, not MOS drive.
        network.P = resistor(command_p, resistance(25));
        network.N = resistor(command_n, resistance(25));
        network.elaborate();
        network.MINUS = resistor(network.PLUS, resistance(100));
        network.PLUS = capacitor(ground, capacitance_value(330e-15));
        network.MINUS = capacitor(ground, capacitance_value(330e-15));
        sim.observe("P", network.P, ground);
        sim.observe("N", network.N, ground);
        sim.observe("PLUS", network.PLUS, ground);
        sim.observe("MINUS", network.MINUS, ground);
        sim.observe("COMMON", network.COMMON, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("COMMAND_P", command_p, ground);
        sim.observe("COMMAND_N", command_n, ground);
        const duration stop(100e-9 + 629 * 8e-9);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination, {stop, duration(step), duration(step / 2), 1e-15, 2, 1e-18});
        if (mode == "run") {
            // Continuations retain charge and history while respecting the
            // solver's 100000-regular-step limit for each integration span.
            sim.run(duration(1e-6), duration(step));
            for (double time = 1e-6; time < stop.value;) {
                time = std::min(stop.value, time + 1e-6);
                sim.advance_to(duration(time), duration(step));
            }
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (mode != "export")
            throw std::runtime_error("use run or export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
