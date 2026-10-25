#include "../differential_receive_gain.hpp"
#include "receive_gain_process_devices.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 6)
            throw std::runtime_error("expected destination run|export gain_code input_common_V step_ps");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned code = std::stoul(argv[3]);
        const double common = std::stod(argv[4]), step = std::stod(argv[5]) * 1e-12;
        if ((mode != "run" && mode != "export") || (code != 4 && code != 8) ||
            common < 1.1 || common > 1.65 || step <= 0 || step > 20e-12)
            throw std::runtime_error("invalid differential-gain experiment");
        constexpr double warmup = 16e-6, stop = warmup + 256e-9;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        electrical ground, supply, rail, positive, negative, reference;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12));
        using wave = std::vector<std::pair<double, double>>;
        wave plus{{0, 0}, {1e-6, 0}, {1.1e-6, common}}, minus = plus;
        const std::array<int, 16> symbols{0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0};
        const double intended = double(code) / (9 - code), amplitude = .15 / intended;
        for (unsigned i = 0; i < 32; ++i) {
            const double time = warmup + i * 8e-9;
            const double half = amplitude * symbols[i % symbols.size()] / 2;
            plus.emplace_back(time, plus.back().second);
            plus.emplace_back(time + .1e-9, common + half);
            minus.emplace_back(time, minus.back().second);
            minus.emplace_back(time + .1e-9, common - half);
        }
        plus.emplace_back(stop, plus.back().second);
        minus.emplace_back(stop, minus.back().second);
        positive = sim_pwl_voltage_source(ground, plus);
        negative = sim_pwl_voltage_source(ground, minus);
        reference = sim_pwl_voltage_source(ground, {{0, 0}, {1e-6, 0}, {1.1e-6, 1.65}});
        one_g::ihp_sg13g2::differential_receive_gain gain;
        gain.Vdd = rail;
        gain.Vss = ground;
        gain.IN_PLUS = resistor(positive, resistance(50));
        gain.IN_MINUS = resistor(negative, resistance(50));
        gain.VREF = resistor(reference, resistance(10));
        gain.VREF = capacitor(ground, capacitance_value(2e-12));
        gain.OUT = capacitor(ground, capacitance_value(3e-12));
        for (unsigned bit = 0; bit < 5; ++bit) {
            electrical control;
            control = sim_voltage_source(ground, voltage(bit == code - 4 ? 0 : 3.3));
            gain.SELECT_N[bit] = resistor(control, resistance(50));
        }
        gain.elaborate();
        sim.observe("IN_MINUS", gain.IN_MINUS, ground);
        sim.observe("OUT", gain.OUT, ground);
        sim.observe("BIAS", gain.BIAS, ground);
        sim.observe("DRIVE", gain.DRIVE, ground);
        sim.observe("FEEDBACK", gain.FEEDBACK, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("IN_PLUS", gain.IN_PLUS, ground);
        sim.observe("VREF", gain.VREF, ground);
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
