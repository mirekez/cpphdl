#include <basic/differential_source_follower.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 7)
            throw std::runtime_error("expected destination run|export supply load_pF return_ohm step_ps");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const double supply = std::stod(argv[3]), load = std::stod(argv[4]) * 1e-12;
        const double return_load = std::stod(argv[5]), step = std::stod(argv[6]) * 1e-12;
        if ((mode != "run" && mode != "export") || supply < 2.97 || supply > 3.63 || load <= 0 ||
            return_load <= 0 || step <= 0 || step > 5e-12)
            throw std::runtime_error("invalid differential follower experiment");
        simulation sim;
        sim.analytic_jacobian = true;
        electrical vss, source, vdd, plus_command, minus_command;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(supply));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(10e-12), voltage(supply));
        std::vector<std::pair<double, double>> plus{{0, 2.8}}, minus{{0, 2.8}};
        unsigned index = 0;
        for (unsigned first = 0; first < 5; ++first)
            for (unsigned second = 0; second < 5; ++second)
                for (unsigned code : {first, second}) {
                    const double time = (4 + 4 * index++) * 1e-9;
                    plus.emplace_back(time, plus.back().second);
                    minus.emplace_back(time, minus.back().second);
                    plus.emplace_back(time + .1e-9, 2.8 + .06 * (int(code) - 2));
                    minus.emplace_back(time + .1e-9, 2.8 - .06 * (int(code) - 2));
                }
        plus_command = sim_pwl_voltage_source(vss, plus);
        minus_command = sim_pwl_voltage_source(vss, minus);
        differential_source_follower follower;
        follower.PLUS = resistor(plus_command, resistance(50));
        follower.MINUS = resistor(minus_command, resistance(50));
        follower.Vdd = vdd;
        follower.Vss = vss;
        follower.initial_supply = voltage(supply);
        follower.elaborate();
        follower.OUTP = capacitor(vss, capacitance_value(load), voltage(1.65));
        follower.OUTN = capacitor(vss, capacitance_value(load), voltage(1.65));
        follower.OUTP = resistor(vss, resistance(return_load));
        follower.OUTN = resistor(vss, resistance(return_load));
        sim.observe("PLUS", follower.PLUS, vss);
        sim.observe("MINUS", follower.MINUS, vss);
        sim.observe("OUTP", follower.OUTP, vss);
        sim.observe("OUTN", follower.OUTN, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("COMMAND_P", plus_command, vss);
        sim.observe("COMMAND_N", minus_command, vss);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(204e-9), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(204e-9), duration(step));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
