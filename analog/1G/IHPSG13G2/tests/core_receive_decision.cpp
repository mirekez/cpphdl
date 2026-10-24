#include "../core_receive_decision.hpp"
#include <VerilogA/experiment.hpp>
#if __has_include(<Eigen/SparseQR>) || __has_include(<eigen3/Eigen/SparseQR>)
#include <sparse.hpp>
#endif
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string_view mode(argv[2]);
        if (mode != "run" && mode != "export")
            throw std::runtime_error("use run or export");
        constexpr double warmup = 8e-6, period = 4e-9;
        constexpr unsigned codes[]{96, 128, 112, 127, 128, 112, 96, 128,
                                   96, 128, 112, 127, 128, 112, 96, 128};
        constexpr double stop = warmup + std::size(codes) * period;
        simulation sim;
        sim.method = integration_method::bdf2;
        sim.analytic_jacobian = true;
#if __has_include(<Eigen/SparseQR>) || __has_include(<eigen3/Eigen/SparseQR>)
        use_sparse_solver(sim, sparse_strategy::certified_lu);
#endif
        sim.minimum_step = duration(.005e-12);
        electrical ground, supply, logic_source, vdd, core;
        sim.reference(ground);
        supply = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 3.3}});
        logic_source = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 1.2}});
        vdd = resistor(supply, resistance(.25));
        core = resistor(logic_source, resistance(10));
        vdd = capacitor(ground, capacitance_value(20e-12));
        core = capacitor(ground, capacitance_value(20e-12));
        one_g::ihp_sg13g2::core_receive_decision dut;
        dut.Vdd = vdd;
        dut.Vlogic = core;
        dut.Vss = ground;
        using wave = std::vector<std::pair<double, double>>;
        auto change = [](wave& points, double time, double value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + .1e-9, value);
        };
        wave input{{0, 0}}, evaluation{{0, 0}};
        std::array<wave, 8> trials;
        for (auto& points : trials)
            points.emplace_back(0, 0);
        for (unsigned index = 0; index < std::size(codes); ++index) {
            const double begin = warmup + index * period;
            // The second pass reverses every decision at the same trial words.
            const bool above = ((index % 2) == 0) != (index >= 8);
            change(input, begin, 3.3 * (codes[index] + (above ? 2.5 : -2.5)) / 256);
            for (unsigned bit = 0; bit < trials.size(); ++bit)
                change(trials[bit], begin, ((codes[index] >> bit) & 1) ? 1.2 : 0);
            change(evaluation, begin + 2e-9, 1.2);
            change(evaluation, begin + 3.8e-9, 0);
        }
        auto drive = [&](port<electrical>& target, const wave& points) {
            electrical source;
            source = sim_pwl_voltage_source(ground, points);
            target = resistor(source, resistance(50));
        };
        drive(dut.IN, input);
        drive(dut.EVALUATE, evaluation);
        drive(dut.CLEAR, {{0, 0}, {.1e-6, 0}, {1e-6, 1.2},
                          {warmup - .1e-6, 1.2}, {warmup - .1e-6 + .1e-9, 0}});
        for (unsigned bit = 0; bit < trials.size(); ++bit)
            drive(dut.TRIAL[bit], trials[bit]);
        // This fixture characterizes a driven input boundary. Acquisition and
        // held-node kickback need the connected sampler regression separately.
        dut.IN = capacitor(ground, capacitance_value(1.5e-12));
        dut.CORE_DECISION = capacitor(ground, capacitance_value(20e-15));
        dut.elaborate();
        sim.observe("IN", dut.IN, ground);
        sim.observe("DAC", dut.dac.OUT, ground);
        sim.observe("PRE_PLUS", dut.preamplifier.OUTP, ground);
        sim.observe("PRE_MINUS", dut.preamplifier.OUTN, ground);
        sim.observe("EVALUATE", dut.EVALUATE, ground);
        sim.observe("CLOCK", dut.timing[0].Y, ground);
        sim.observe("POSITIVE", dut.comparator.OUTP, ground);
        sim.observe("NEGATIVE", dut.comparator.OUTN, ground);
        sim.observe("DECISION", dut.decision.Q, ground);
        sim.observe("CORE_DECISION", dut.CORE_DECISION, ground);
        sim.observe("VDD", vdd, ground);
        sim.observe("VLOGIC", core, ground);
        sim.observe("BIAS", dut.dac.BIAS, ground);
        sim.observe("CLEAR", dut.CLEAR, ground);
        sim.observe("CLEAR_HV", dut.timing[1].Y, ground);
        sim.observe("RETURN_LOW", dut.core_interface.inverted, ground);
        for (unsigned i = 0; i < 10; ++i) {
            auto& control = i < 8 ? dut.dac.controls[i] : dut.timing[i - 8];
            for (const auto& entry : std::initializer_list<std::pair<std::string, electrical>>{
                    {"CONTROL", control.A}, {"COMPLEMENT", control.complement},
                    {"LOW", control.low_node}, {"LOW_N", control.low_node_n}})
                sim.observe(entry.first + std::to_string(i), entry.second, ground);
        }
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(.5e-12), duration(.5e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup - .2e-6), duration(1e-9));
            sim.advance_to(duration(warmup), duration(10e-12));
            sim.discard_trace();
            // Preserve electrical state while bounding each advance below
            // the engine's per-call integration-step limit.
            for (unsigned index = 1; index <= std::size(codes); ++index) {
                sim.advance_to(duration(warmup + index * period), duration(.125e-12));
                std::cout << "Completed conversion " << index << " of " << std::size(codes) << std::endl;
            }
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
