#include <basic/inverter.hpp>
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
            throw std::runtime_error("expected run or export");
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.05e-12);
        electrical ground, source, rail, input_source;
        sim.reference(ground);
        source = sim_voltage_source(ground, voltage(1.2));
        rail = resistor(source, resistance(10));
        rail = capacitor(ground, capacitance_value(20e-12), voltage(1.2));
        std::vector<std::pair<double, double>> points{{0, 0}};
        for (unsigned i = 0; i < 6; ++i) {
            const double time = (1 + 4 * i) * 1e-9;
            points.emplace_back(time, 0);
            points.emplace_back(time + .1e-9, 1.2);
            points.emplace_back(time + 2e-9, 1.2);
            points.emplace_back(time + 2.1e-9, 0);
        }
        input_source = sim_pwl_voltage_source(ground, points);
        cmos<channel_model> inverter;
        inverter.A = resistor(input_source, resistance(50));
        inverter.Y = capacitor(ground, capacitance_value(.16e-12), voltage(1.2));
        inverter.Vdd = rail;
        inverter.Vss = ground;
        inverter.elaborate();
        sim.observe("A", inverter.A, ground);
        sim.observe("Y", inverter.Y, ground);
        sim.observe("VDD", rail, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(24e-9), duration(1e-12), duration(.5e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(24e-9), duration(1e-12));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
