#include "../reference_clock.hpp"
#include <VerilogA/experiment.hpp>
#include <cpphdl_analog.h>
#include "clock_counter.hpp"
#if __has_include(<Eigen/SparseLU>) || __has_include(<eigen3/Eigen/SparseLU>)
#include <sparse.hpp>
#define CPPHDL_REFERENCE_SPARSE
#endif
#include <iostream>
#include <fstream>
#include <iomanip>

using namespace analog;
long _system_clock = -1;

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "server") {
        return run_clock_counter_server();
    }
    if (argc != 5 && argc != 6)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const double ppm = std::stod(argv[3]);
        const unsigned load = unsigned(std::stoul(argv[4]));
        if (load > 2)
            throw std::runtime_error("invalid output load");
        const bool refined = argc == 6 && std::stoi(argv[5]) != 0;
        const double stop_time = refined ? 480e-9 : 960e-9;
        const double period = 8e-9 / (1 + ppm * 1e-6);
        simulation sim;
        sim.analytic_jacobian = true;
#ifdef CPPHDL_REFERENCE_SPARSE
        use_sparse_solver(sim, sparse_strategy::certified_lu);
#endif
        sim.minimum_step = duration(8e-9 / 65536);
        electrical source, vdd, vss, reference, clear_source;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(1.2));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(2e-12));
        std::vector<std::pair<double, double>> wave{{0, 0}};
        double previous = 0;
        for (unsigned half = 1; half * period / 2 + .1e-9 < stop_time; ++half) {
            const double time = half * period / 2;
            wave.push_back({time, previous});
            previous = half & 1 ? 1.2 : 0;
            wave.push_back({time + .1e-9, previous});
        }
        reference = sim_pwl_voltage_source(vss, wave);
        one_g::reference_clock_multiplier clock;
        clock.REF = resistor(reference, resistance(50));
        clear_source = sim_pwl_voltage_source(vss, {{0, 1.2}, {32e-9, 1.2}, {32.1e-9, 0}});
        clock.CLEAR = resistor(clear_source, resistance(50));
        clock.Vdd = vdd;
        clock.Vss = vss;
        clock.elaborate();
        if (load)
            clock.OUT = capacitor(vss, capacitance_value(load == 1 ? 20e-15 : 2e-12));
        sim.observe("OUT", clock.OUT, vss);
        sim.observe("DIVIDED", clock.DIVIDED, vss);
        sim.observe("REF", clock.REF, vss);
        sim.observe("CONTROL", clock.CONTROL, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("UP", clock.detector.UP, vss);
        sim.observe("DOWN", clock.detector.DOWN, vss);
        std::filesystem::create_directories(destination);
        const duration stop(stop_time), step(refined ? 5e-12 : 10e-12);
        verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
        std::ofstream(destination / "live.json") << "{\"clock\":\"v(n" << sim.root(clock.OUT.id())
            << ")\",\"stop_s\":" << std::setprecision(17) << stop_time << "}\n";
        if (std::string_view(argv[2]) == "run") {
            bits<1> achieved_clock;
            achieved_clock = sim_schmitt_sense(clock.OUT, vss, voltage(.36), voltage(.84));
            auto controller = std::make_shared<ClockEdgeCounter>();
            sim_clock_cpphdl(controller, achieved_clock);
            sim.run(stop, step);
            sim.write(destination / "cpp");
            ++_system_clock;
            std::ofstream(destination / "clock_controller.json") << "{\"count\":"
                << static_cast<std::uint64_t>(controller->Q()) << "}\n";
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
