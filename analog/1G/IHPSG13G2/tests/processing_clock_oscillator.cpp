#include "../processing_clock_oscillator.hpp"
#include <VerilogA/experiment.hpp>
#include <sparse.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 6)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string mode(argv[2]);
        const unsigned trim = std::stoul(argv[3]);
        const double load = std::stod(argv[4]);
        const bool refined = std::stoi(argv[5]) != 0;
        if (trim > 65535 || !std::isfinite(load) || load < 0 || load > 1e-12)
            throw std::runtime_error("invalid processing clock experiment");
        simulation sim;
        sim.analytic_jacobian = true;
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        sim.minimum_step = duration(1e-13);
        electrical ground, supply, vdd;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(1.2));
        vdd = resistor(supply, resistance(10));
        one_g::ihp_sg13g2::processing_clock_oscillator clock;
        for (unsigned bit = 0; bit < 8; ++bit) {
            clock.COARSE_N[bit] = sim_voltage_source(ground, voltage((trim >> (bit + 8)) & 1 ? 0 : 1.2));
            clock.FINE_N[bit] = sim_voltage_source(ground, voltage((trim >> bit) & 1 ? 0 : 1.2));
        }
        clock.CLEAR = sim_pwl_voltage_source(ground, {{0, 1.2}, {32e-9, 1.2}, {32.1e-9, 0}});
        clock.Vdd = vdd;
        clock.Vss = ground;
        clock.elaborate();
        clock.OUT = capacitor(ground, capacitance_value(load));
        sim.observe("OUT", clock.OUT, ground);
        sim.observe("RING", clock.RING, ground);
        sim.observe("CONTROL", clock.CONTROL, ground);
        sim.observe("VDD", vdd, ground);
        sim.observe("CLEAR", clock.CLEAR, ground);
        const duration stop(512e-9), step(refined ? 5e-12 : 10e-12);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
        if (mode == "export")
            return 0;
        if (mode != "run")
            throw std::runtime_error("use run or export");
        // Preserve circuit state across windows below the solver's step budget.
        sim.run(duration(256e-9), step);
        sim.advance_to(stop, step);
        sim.write(destination / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
