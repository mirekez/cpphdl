#include "../shared_receive_track.hpp"
#include <VerilogA/experiment.hpp>
#if __has_include(<Eigen/SparseQR>) || __has_include(<eigen3/Eigen/SparseQR>)
#include <sparse.hpp>
#endif
#include <iostream>

using namespace analog;

// Independent loaded clock experiment, not a bank qualification. The nominal
// closing aperture is the command's half-rail time; translator delay must be
// measured against that fixed boundary, never absorbed into the input target.
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string_view mode(argv[2]);
        if (mode != "run" && mode != "export")
            throw std::runtime_error("use run or export");
        constexpr double warmup = 8e-6, period = 4e-9, edge = .4e-9;
        constexpr unsigned events = 32;
        constexpr double stop = warmup + events * period;
        simulation sim;
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
        one_g::ihp_sg13g2::shared_receive_track dut;
        dut.Vdd = vdd;
        dut.Vlogic = core;
        dut.Vss = ground;
        using wave = std::vector<std::pair<double, double>>;
        auto change = [](wave& points, double time, double value) {
            if (points.back().second != value) {
                points.emplace_back(time, points.back().second);
                points.emplace_back(time + edge, value);
            }
        };
        wave input{{0, 0}, {1e-6, 1.6}}, strobe{{0, 0}};
        wave enable{{0, 0}}, precharge{{0, 0}}, allow{{0, 0}};
        std::array<wave, 13> selections;
        for (auto& points : selections)
            points.emplace_back(0, 0);
        change(enable, 2e-6, 1.2);
        change(precharge, 2e-6 + 20e-9, 1.2);
        // Separate physical clear interval after all-track startup.
        change(enable, warmup - 24e-9, 0);
        change(precharge, warmup - 20e-9, 0);
        change(enable, warmup - 12e-9, 1.2);
        for (unsigned k = 0; k < events; ++k) {
            const double begin = warmup + k * period;
            const unsigned selected = (k + 10) % 13;
            const bool permitted = k > 0 && k < 26 && k != 7 && k != 8;
            // Setup precedes the nominal shared event by 2 ns. Event 20
            // deliberately requests open and close for the same slice.
            for (unsigned i = 0; i < selections.size(); ++i)
                change(selections[i], begin - 2e-9,
                       (i == selected || (k == 20 && i == (selected + 11) % 13)) ? 1.2 : 0);
            change(allow, begin - 2e-9, permitted ? 1.2 : 0);
            change(strobe, begin, 1.2);
            change(strobe, begin + 1.5e-9, 0);
        }
        // Disable with acquisitions outstanding, then restart without precharge.
        change(enable, warmup + 23 * period - 2e-9, 0);
        change(enable, warmup + 25 * period - 2e-9, 1.2);
        input.emplace_back(warmup - period, 1.6);
        for (unsigned k = 0; k <= events; ++k)
            input.emplace_back(warmup + k * period, (k % 4 < 2) ? 1.4 : 1.8);
        auto drive = [&](port<electrical>& target, const wave& points) {
            electrical source;
            source = sim_pwl_voltage_source(ground, points);
            target = resistor(source, resistance(50));
        };
        drive(dut.IN, input);
        drive(dut.STROBE_CORE, strobe);
        drive(dut.ENABLE_CORE, enable);
        drive(dut.PRECHARGE_CORE, precharge);
        drive(dut.OPEN_ALLOW_CORE, allow);
        for (unsigned i = 0; i < selections.size(); ++i)
            drive(dut.SELECT_CORE[i], selections[i]);
        dut.elaborate();
        sim.observe("IN", dut.IN, ground);
        sim.observe("VDD", vdd, ground);
        sim.observe("VLOGIC", core, ground);
        sim.observe("BIAS", dut.BIAS, ground);
        sim.observe("STROBE_CORE", dut.STROBE_CORE, ground);
        sim.observe("STROBE", dut.STROBE, ground);
        for (unsigned i = 0; i < dut.slices; ++i) {
            for (const auto& item : std::initializer_list<std::pair<std::string, electrical>>{
                    {"SELECT", dut.SELECT_CORE[i]}, {"TRACK", dut.TRACK[i]},
                    {"CLOCK_N", dut.CLOCK_N[i]}, {"HELD", dut.HELD[i]},
                    {"STATE_N", dut.STATE_N[i]}, {"SET_N", dut.SET_N[i]},
                    {"RESET_N", dut.RESET_N[i]}})
                sim.observe(item.first + std::to_string(i), item.second, ground);
        }
        for (unsigned i = 0; i < dut.interfaces; ++i) {
            for (const auto& item : std::initializer_list<std::pair<std::string, electrical>>{
                    {"CONTROL", dut.CONTROL[i]}, {"INPUT_N", dut.INPUT_N[i]},
                    {"LOW", dut.LOW[i]}, {"LOW_N", dut.LOW_N[i]}})
                sim.observe(item.first + std::to_string(i), item.second, ground);
        }
        sim.observe("EVENT", dut.EVENT, ground);
        sim.observe("EVENT_DELAY", dut.EVENT_DELAY, ground);
        sim.observe("EVENT_DELAY_N", dut.EVENT_DELAY_N, ground);
        sim.observe("EVENT_NAND", dut.EVENT_NAND, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(2e-12), duration(2e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup - 32e-9), duration(1e-9));
            sim.discard_trace();
            for (unsigned k = 0; k < events + 8; ++k)
                sim.advance_to(duration(warmup - 32e-9 + (k + 1) * period), duration(2e-12));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
