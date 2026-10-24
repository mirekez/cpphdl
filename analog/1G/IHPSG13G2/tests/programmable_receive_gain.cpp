#include "../programmable_receive_gain.hpp"
#include <VerilogA/experiment.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace analog;

#include "receive_gain_process_devices.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::runtime_error("expected destination run|export gain_code step_ps");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned code = std::stoul(argv[3]);
        const double step = std::stod(argv[4]) * 1e-12;
        if ((mode != "run" && mode != "export") || (code < 4 || code > 8) || !std::isfinite(step) || step <= 0)
            throw std::runtime_error("invalid gain experiment");
        constexpr double warmup = 16e-6, stop = warmup + .16e-6;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        electrical ground, supply, rail, input_source;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12));
        const std::array<double, 16> symbols{0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0};
        const double amplitude = .15 * (10 - code) / code;
        std::vector<std::pair<double, double>> points{{0, 0}, {1e-6, 0}, {1.1e-6, 1.4}};
        for (unsigned i = 0; i < symbols.size(); ++i) {
            const double begin = warmup + i * 8e-9;
            const double value = 1.4 + amplitude * symbols[i] / 2;
            points.emplace_back(begin, points.back().second);
            points.emplace_back(begin + .1e-9, value);
        }
        points.emplace_back(stop, 1.4);
        input_source = sim_pwl_voltage_source(ground, points);
        one_g::ihp_sg13g2::programmable_receive_gain gain;
        gain.IN = resistor(input_source, resistance(50));
        gain.OUT = capacitor(ground, capacitance_value(3e-12));
        gain.Vdd = rail;
        gain.Vss = ground;
        for (unsigned bit = 0; bit < 5; ++bit) {
            electrical control;
            control = sim_voltage_source(ground, voltage(bit == code - 4 ? 0 : 3.3));
            gain.SELECT_N[bit] = resistor(control, resistance(50));
        }
        gain.elaborate();
        sim.observe("IN", gain.IN, ground);
        sim.observe("OUT", gain.OUT, ground);
        sim.observe("BIAS", gain.BIAS, ground);
        sim.observe("DRIVE", gain.DRIVE, ground);
        sim.observe("FEEDBACK", gain.FEEDBACK, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("LAST_TAP", gain.TAP[7], ground);
        sim.observe("SELECT_N", gain.SELECT_N[code - 4], ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(step), duration(step), 1e-16, 2, 1e-18});
        write_process_devices(sim, rail, destination);
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
