#ifdef CPPHDL_BUFFERED_NAND_LATCH
#include <basic/buffered_nand_latch.hpp>
using TestLatch = analog::buffered_nand_latch;
#else
#include <basic/nand_latch.hpp>
using TestLatch = analog::cleared_nand_latch;
#endif
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("expected destination run|export loaded [step_ps]");
        const double step = argc == 5 ? std::stod(argv[4]) * 1e-12 : 2e-12;
        if (!(step > 0 && step <= 2e-12))
            throw std::runtime_error("latch timestep must be positive and at most 2 ps");
        simulation sim;
        sim.analytic_jacobian = true;
        electrical source, vdd, vss;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(3.3));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(10e-12), voltage(3.3));
        TestLatch latch;
        latch.Vdd = vdd;
        latch.Vss = vss;
        using wave = std::vector<std::pair<double, double>>;
        auto input = [&](wave points) {
            electrical stimulus;
            stimulus = sim_pwl_voltage_source(vss, points);
            electrical terminal;
            terminal = resistor(stimulus, resistance(50));
            return terminal;
        };
        latch.CLEAR = input({{0, 3.3}, {3e-9, 3.3}, {3.1e-9, 0}});
        latch.SET_N = input({{0, 3.3}, {6e-9, 3.3}, {6.1e-9, 0}, {10e-9, 0},
                             {10.1e-9, 3.3}, {22e-9, 3.3}, {22.1e-9, 0}, {26e-9, 0}, {26.1e-9, 3.3}});
        latch.RESET_N = input({{0, 3.3}, {14e-9, 3.3}, {14.1e-9, 0}, {18e-9, 0},
                               {18.1e-9, 3.3}, {22e-9, 3.3}, {22.1e-9, 0}, {24e-9, 0}, {24.1e-9, 3.3}});
        latch.elaborate();
        if (std::string_view(argv[3]) == "1") {
            latch.Q = capacitor(vss, capacitance_value(.2e-12));
            latch.Q = resistor(vss, resistance(10000));
        }
        sim.observe("Q", latch.Q, vss);
        sim.observe("Q_N", latch.Q_N, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("SET_N", latch.SET_N, vss);
        sim.observe("RESET_N", latch.RESET_N, vss);
        sim.observe("CLEAR", latch.CLEAR, vss);
        const std::filesystem::path destination(argv[1]);
        if (std::string_view(argv[2]) == "run") {
            sim.run(duration(32e-9), duration(step));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
        verilog_a::write_experiment(sim, destination,
            {duration(32e-9), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
