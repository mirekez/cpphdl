#include "../differential_receive_frontend.hpp"
#include "../receive_reference.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 5 && argc != 6)
            throw std::runtime_error("expected destination run|export gain_code step_ps [line_common_V]");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned code = std::stoul(argv[3]);
        const double step = std::stod(argv[4]) * 1e-12;
        const double common = argc == 6 ? std::stod(argv[5]) : 2.3;
        if ((mode != "run" && mode != "export") || (code != 4 && code != 8) || step <= 0 || step > 5e-12 || common < 2.1 || common > 2.5)
            throw std::runtime_error("invalid MDI preamplifier experiment");
        constexpr double warmup = 16e-6, stop = warmup + 256e-9;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        electrical ground, supply, rail, positive, negative;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12));
        std::vector<std::pair<double, double>> plus{{0, 0}, {1e-6, 0}, {1.1e-6, common}}, minus = plus;
        const std::array<int, 16> symbols{0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0};
        const double amplitude = code == 4 ? 1 : .125;
        for (unsigned i = 0; i < 32; ++i) {
            const double time = warmup + i * 8e-9, half = amplitude * symbols[i % 16] / 2;
            plus.emplace_back(time, plus.back().second);
            minus.emplace_back(time, minus.back().second);
            plus.emplace_back(time + .1e-9, common + half);
            minus.emplace_back(time + .1e-9, common - half);
        }
        plus.emplace_back(stop, plus.back().second);
        minus.emplace_back(stop, minus.back().second);
        positive = sim_pwl_voltage_source(ground, plus);
        negative = sim_pwl_voltage_source(ground, minus);
        one_g::mdi_input_network input;
        one_g::ihp_sg13g2::differential_receive_preamplifier preamplifier;
        one_g::ihp_sg13g2::receive_reference reference;
        input.Vdd = rail;
        input.Vss = ground;
        input.P = resistor(positive, resistance(25));
        input.N = resistor(negative, resistance(25));
        input.elaborate();
        reference.Vdd = rail;
        reference.Vss = ground;
        reference.elaborate();
        preamplifier.PLUS = input.PLUS;
        preamplifier.MINUS = input.MINUS;
        preamplifier.VREF = reference.OUT;
        preamplifier.Vdd = rail;
        preamplifier.Vss = ground;
        for (unsigned bit = 0; bit < 5; ++bit) {
            electrical control;
            control = sim_voltage_source(ground, voltage(bit == code - 4 ? 0 : 3.3));
            preamplifier.SELECT_N[bit] = resistor(control, resistance(50));
        }
        preamplifier.elaborate();
        sim.observe("IN_MINUS", preamplifier.gain.IN_MINUS, ground);
        sim.observe("OUT", preamplifier.OUT, ground);
        sim.observe("BIAS", preamplifier.gain.BIAS, ground);
        sim.observe("DRIVE", preamplifier.gain.DRIVE, ground);
        sim.observe("FEEDBACK", preamplifier.gain.FEEDBACK, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("IN_PLUS", preamplifier.gain.IN_PLUS, ground);
        sim.observe("VREF", reference.OUT, ground);
        sim.observe("P", input.P, ground);
        sim.observe("N", input.N, ground);
        sim.observe("SENSE_PLUS", input.PLUS, ground);
        sim.observe("SENSE_MINUS", input.MINUS, ground);
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
