#include <basic/level_down.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 7)
        return 2;
    try {
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const double core = std::stod(argv[3]), input = std::stod(argv[4]);
        const double load = std::stod(argv[5]) * 1e-15;
        const double step = std::stod(argv[6]) * 1e-12;
        if (core < 1.08 || core > 1.32 || input < 2.97 || input > 3.63 ||
            load <= 0 || step <= 0 || step > 2e-12)
            throw std::runtime_error("invalid level-down experiment");
        simulation sim;
        sim.analytic_jacobian = true;
        electrical vss, source, vdd, command;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(core));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(2e-12));
        std::vector<std::pair<double, double>> wave{{0, 0}};
        double previous = 0;
        for (unsigned i = 1; i < 16; ++i) {
            const double next = i & 1 ? input : 0;
            wave.emplace_back(i * 2e-9, previous);
            wave.emplace_back(i * 2e-9 + .1e-9, next);
            previous = next;
        }
        command = sim_pwl_voltage_source(vss, wave);
        level_down interface;
        interface.Vdd = vdd;
        interface.Vss = vss;
        interface.A = resistor(command, resistance(50));
        interface.elaborate();
        interface.Y = capacitor(vss, capacitance_value(load));
        interface.Y = resistor(vss, resistance(10000));
        sim.observe("Y", interface.Y, vss);
        sim.observe("INVERTED", interface.inverted, vss);
        sim.observe("A", interface.A, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("COMMAND", command, vss);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(32e-9), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(32e-9), duration(step));
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
