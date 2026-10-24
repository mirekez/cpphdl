#include <basic/nmos_source_follower.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 7)
        return 2;
    try {
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const double supply = std::stod(argv[3]), load = std::stod(argv[4]) * 1e-12;
        const double return_load = std::stod(argv[5]), step = std::stod(argv[6]) * 1e-12;
        if (supply < 2.97 || supply > 3.63 || load <= 0 || return_load <= 0 || step <= 0 || step > 5e-12)
            throw std::runtime_error("invalid follower experiment");
        simulation sim;
        sim.analytic_jacobian = true;
        electrical vss, source, vdd, command;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(supply));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(10e-12));
        // Include every ordered PAM-level transition and a full-scale fall.
        std::vector<std::pair<double, double>> wave{{0, 0}, {1e-9, 0}, {1.1e-9, 2.5}};
        double previous = 2.5;
        unsigned index = 0;
        for (unsigned first = 0; first < 5; ++first)
            for (unsigned second = 0; second < 5; ++second)
                for (unsigned code : {first, second}) {
                    const double time = (4 + 4 * index++) * 1e-9;
                    const double next = 2.5 + .2 * (int(code) - 2);
                    wave.emplace_back(time, previous);
                    wave.emplace_back(time + .1e-9, next);
                    previous = next;
                }
        command = sim_pwl_voltage_source(vss, wave);
        nmos_source_follower follower;
        follower.IN = resistor(command, resistance(50));
        follower.Vdd = vdd;
        follower.Vss = vss;
        follower.elaborate();
        follower.OUT = capacitor(vss, capacitance_value(load));
        follower.OUT = resistor(vss, resistance(return_load));
        sim.observe("OUT", follower.OUT, vss);
        sim.observe("IN", follower.IN, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("COMMAND", command, vss);
        std::filesystem::create_directories(destination);
        const duration stop((4 + 4 * index) * 1e-9);
        verilog_a::write_experiment(sim, destination,
            {stop, duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(stop, duration(step));
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
