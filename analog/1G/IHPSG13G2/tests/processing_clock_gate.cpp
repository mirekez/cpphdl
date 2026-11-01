#include "../processing_clock_gate.hpp"
#include <VerilogA/experiment.hpp>
#include <sparse.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string_view mode(argv[2]);
        if (mode != "run" && mode != "export")
            throw std::runtime_error("use run or export");
        std::filesystem::create_directories(destination);
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(1e-14);
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        electrical ground, supply, vdd;
        sim.reference(ground);
        supply = sim_pwl_voltage_source(ground, {{0, 0}, {2e-9, 1.2}});
        vdd = resistor(supply, resistance(1));
        vdd = capacitor(ground, capacitance_value(1e-12));
        one_g::ihp_sg13g2::processing_clock_gate gate;
        gate.Vdd = vdd;
        gate.Vss = ground;
        using waveform = std::vector<std::pair<double, double>>;
        waveform clock{{0, 0}}, enable{{0, 0}};
        auto change = [](waveform& points, double time, double value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + 50e-12, value);
        };
        std::ofstream expected(destination / "pulses.csv");
        expected << "cycle,rise_s,fall_s,admitted\n" << std::setprecision(17);
        double time = 10e-9;
        for (unsigned cycle = 0; cycle < 96; ++cycle) {
            const bool acquiring = cycle < 16 || (cycle >= 48 && cycle < 64);
            const double period = acquiring ? 2.5e-9 : 1.0 / 134e6;
            change(clock, time, 1.2);
            change(clock, time + period / 2, 0);
            // High-phase admission must wait; high-phase withdrawal must finish
            // the current pulse. Low-phase requests affect the following edge.
            if (cycle == 20) change(enable, time + period / 4, 1.2);
            if (cycle == 36) change(enable, time + period / 4, 0);
            if (cycle == 68) change(enable, time + 3 * period / 4, 1.2);
            if (cycle == 84) change(enable, time + 3 * period / 4, 0);
            if (cycle == 22) {
                change(enable, time + period / 8, 0);
                change(enable, time + period / 4, 1.2);
            }
            const bool admitted = (cycle >= 21 && cycle <= 36) || (cycle >= 69 && cycle <= 84);
            expected << cycle << ',' << time + 25e-12 << ',' << time + period / 2 + 25e-12
                     << ',' << admitted << '\n';
            time += period;
        }
        electrical clock_source, enable_source;
        clock_source = sim_pwl_voltage_source(ground, clock);
        enable_source = sim_pwl_voltage_source(ground, enable);
        gate.CLOCK = resistor(clock_source, resistance(50));
        gate.ENABLE = resistor(enable_source, resistance(50));
        gate.elaborate();
        gate.OUT = capacitor(ground, capacitance_value(20e-15));
        sim.observe("CLOCK", gate.CLOCK, ground);
        sim.observe("ENABLE", gate.ENABLE, ground);
        sim.observe("OUT", gate.OUT, ground);
        sim.observe("HELD", gate.admission.Q, ground);
        sim.observe("VDD", vdd, ground);
        verilog_a::write_experiment(sim, destination,
            {duration(time + 5e-9), duration(10e-12), duration(5e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(time + 5e-9), duration(10e-12));
            sim.write(destination / "cpp");
            if (sim.status() != "sampled_pass")
                throw std::runtime_error("clock-gate electrical solve failed");
        }
        if (!expected)
            throw std::runtime_error("incomplete independent pulse schedule");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
