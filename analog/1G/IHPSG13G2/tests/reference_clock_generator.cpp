#include "../reference_clock_generator.hpp"
#include "../../tests/clock_counter.hpp"
#include <VerilogA/experiment.hpp>
#include <cpphdl_analog.h>
#include <sparse.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace analog;
long _system_clock = -1;

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "server")
        return run_clock_counter_server();
    if (argc != 6)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string mode(argv[2]);
        const double ppm = std::stod(argv[3]);
        const unsigned load = std::stoul(argv[4]);
        const bool refined = std::stoi(argv[5]) != 0;
        if (load > 2 || !std::isfinite(ppm))
            throw std::runtime_error("invalid clock-domain experiment parameters");
        const double stop = 2e-6;
        const double period = 8e-9 / (1 + ppm * 1e-6);
        const double step = refined ? 10e-12 : 20e-12;
        simulation sim;
        sim.analytic_jacobian = true;
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        sim.minimum_step = duration(period / 65536);
        electrical ground, logic_source, io_source, logic_supply, io_supply, reference, clear;
        sim.reference(ground);
        logic_source = sim_voltage_source(ground, voltage(1.2));
        io_source = sim_voltage_source(ground, voltage(3.3));
        logic_supply = resistor(logic_source, resistance(10));
        logic_supply = capacitor(ground, capacitance_value(2e-12));
        io_supply = resistor(io_source, resistance(10));
        io_supply = capacitor(ground, capacitance_value(2e-12));
        std::vector<std::pair<double, double>> wave{{0, 0}};
        double previous = 0;
        for (unsigned half = 1; half * period / 2 + .1e-9 < stop; ++half) {
            const double time = half * period / 2;
            wave.push_back({time, previous});
            previous = half & 1 ? 1.2 : 0;
            wave.push_back({time + .1e-9, previous});
        }
        reference = sim_pwl_voltage_source(ground, wave);
        clear = sim_pwl_voltage_source(ground, {{0, 1.2}, {32e-9, 1.2}, {32.1e-9, 0}});
        one_g::ihp_sg13g2::reference_clock_generator domain;
        domain.REF = resistor(reference, resistance(50));
        domain.CLEAR = resistor(clear, resistance(50));
        domain.Vlogic = logic_supply;
        domain.Vdd = io_supply;
        domain.Vss = ground;
        domain.elaborate();
        // Output loading acts back through Miller charge, the shared bias,
        // the translator input gates and both finite supply impedances.
        domain.OUT = capacitor(ground, capacitance_value(load == 2 ? 20e-12 : load == 1 ? .2e-12 : 20e-15));
        sim.observe("LOW", domain.multiplier.OUT, ground);
        sim.observe("OUT", domain.OUT, ground);
        sim.observe("OUT_N", domain.OUT_N, ground);
        sim.observe("DIVIDED", domain.multiplier.DIVIDED, ground);
        sim.observe("REF", domain.REF, ground);
        sim.observe("CONTROL", domain.multiplier.CONTROL, ground);
        sim.observe("VLOGIC", logic_supply, ground);
        sim.observe("VDD", io_supply, ground);
        sim.observe("BIAS", domain.translator.BIAS, ground);
        sim.observe("UP", domain.multiplier.detector.UP, ground);
        sim.observe("DOWN", domain.multiplier.detector.DOWN, ground);
        sim.observe("PREDRIVE", domain.predriver.Y, ground);
        sim.observe("BUFFERED", domain.driver.Y, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination, {duration(stop), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        std::ofstream(destination / "live.json") << "{\"clock\":\"v(n" << sim.root(domain.OUT.id())
            << ")\",\"stop_s\":" << std::setprecision(17) << stop << "}\n";
        if (mode == "export")
            return 0;
        if (mode != "run")
            throw std::runtime_error("use run or export");
        bits<1> achieved_clock;
        achieved_clock = sim_schmitt_sense(domain.OUT, ground, voltage(.99), voltage(2.31));
        auto controller = std::make_shared<ClockEdgeCounter>();
        sim_clock_cpphdl(controller, achieved_clock);
        std::filesystem::create_directories(destination / "cpp");
        std::ofstream trace(destination / "cpp/trace.csv");
        trace << "time_s";
        for (const auto& observation : sim.observations())
            trace << ',' << observation.name;
        trace << '\n' << std::setprecision(17);
        // Retain the PLL charge and the actual clocked counter across windows.
        // Even the refined grid stays below the per-window step budget.
        for (double current = 0; current < stop;) {
            const double next = std::min(stop, current + 128e-9);
            if (current == 0)
                sim.run(duration(next), duration(step));
            else
                sim.advance_to(duration(next), duration(step));
            for (const auto& row : sim.trace) {
                trace << row.time;
                for (const auto& value : row.values) {
                    if (!value)
                        throw std::runtime_error("unknown connected-clock observation");
                    trace << ',' << *value;
                }
                trace << '\n';
            }
            sim.discard_trace();
            current = next;
        }
        ++_system_clock;
        std::ofstream(destination / "clock_controller.json") << "{\"count\":"
            << static_cast<std::uint64_t>(controller->Q()) << "}\n";
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
