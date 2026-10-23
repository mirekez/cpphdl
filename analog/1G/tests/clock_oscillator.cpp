#include "../receive_clock_oscillator.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const unsigned code = unsigned(std::stoul(argv[3]));
        const bool loaded = std::stoi(argv[4]) != 0;
        const unsigned refinement = unsigned(std::stoul(argv[5]));
        const unsigned fine = argc == 7 ? unsigned(std::stoul(argv[6])) : 128;
        if (code > 255 || fine > 255 || (refinement != 1 && refinement != 2))
            throw std::runtime_error("invalid oscillator experiment");
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(8e-9 / 65536);
        electrical source, vdd, vss;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(1.2));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(2e-12));
        one_g::receive_clock_oscillator clock;
        for (unsigned bit = 0; bit < 8; ++bit) {
            clock.COARSE_N[bit] = sim_voltage_source(vss, voltage((code >> bit) & 1 ? 0 : 1.2));
            clock.FINE_N[bit] = sim_voltage_source(vss, voltage((fine >> bit) & 1 ? 0 : 1.2));
        }
        clock.RESET = sim_pwl_voltage_source(vss, {{0, 1.2}, {16e-9, 1.2}, {16.1e-9, 0}});
        clock.Vdd = vdd;
        clock.Vss = vss;
        clock.elaborate();
        if (loaded)
            clock.OUT = capacitor(vss, capacitance_value(.02e-12));
        sim.observe("OUT", clock.OUT, vss);
        sim.observe("CONTROL", clock.CONTROL, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("RESET", clock.RESET, vss);
        const duration stop(512e-9), step(8e-9 / (768 * refinement));
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
        if (std::string_view(argv[2]) == "run") {
            sim.run(stop, step);
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
