#include "../core_receive_slice.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected test directory");
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(1e-14);
        electrical ground, supply, vdd, clock_source, enable_source;
        sim.reference(ground);
        supply = sim_pwl_voltage_source(ground, {{0, 0}, {2e-9, 1.2}});
        vdd = resistor(supply, resistance(1));
        vdd = capacitor(ground, capacitance_value(5e-12));
        one_g::ihp_sg13g2::receive_evaluation_gate gate;
        gate.Vdd = vdd;
        gate.Vss = ground;
        using waveform = std::vector<std::pair<double, double>>;
        waveform clock{{0, 0}}, enable{{0, 0}};
        const auto change = [](waveform& points, double time, double value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + 50e-12, value);
        };
        for (unsigned cycle = 0; cycle < 32; ++cycle) {
            const double start = 10e-9 + cycle * 4e-9;
            change(clock, start, 1.2);
            change(clock, start + 2e-9, 0);
            if (cycle == 4 || cycle == 16) change(enable, start + .2e-9, 1.2);
            if (cycle == 12 || cycle == 24) change(enable, start + .2e-9, 0);
        }
        clock_source = sim_pwl_voltage_source(ground, clock);
        enable_source = sim_pwl_voltage_source(ground, enable);
        gate.CLOCK = resistor(clock_source, resistance(50));
        gate.ENABLE = resistor(enable_source, resistance(50));
        gate.elaborate();
        // Thirteen translator inputs each declare 40 fF to VSS and 16 fF
        // gate/drain coupling. Grounding that coupling is only a load screen;
        // real translator feedback and routed capacitance need separate checks.
        gate.OUT = capacitor(ground, capacitance_value(13 * 56e-15));
        sim.observe("CLOCK", gate.CLOCK, ground);
        sim.observe("ENABLE", gate.ENABLE, ground);
        sim.observe("OUT", gate.OUT, ground);
        sim.observe("VDD", vdd, ground);
        const std::filesystem::path work(argv[1]);
        verilog_a::write_experiment(sim, work,
            {duration(138e-9), duration(10e-12), duration(5e-12), 1e-16, 2, 1e-18});
        sim.run(duration(138e-9), duration(10e-12));
        sim.write(work / "cpp");
        if (sim.status() != "sampled_pass") throw std::runtime_error("evaluation gate solve failed");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
