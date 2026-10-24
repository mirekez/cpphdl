#include "../receive_aperture_sensor.hpp"
#include <VerilogA/experiment.hpp>
#include <sparse.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const std::string mode(argv[2]), scenario(argv[3]);
        if (mode != "run" && mode != "export") throw std::runtime_error("run or export required");
        double phase = 0, feedback_period = 4e-9, edge = .4e-9;
        double core_voltage = 1.2, sample_voltage = 3.3;
        if (scenario == "early") phase = -.3e-9;
        else if (scenario == "late") phase = .3e-9;
        else if (scenario == "wrong_ratio") feedback_period = 8e-9;
        else if (scenario == "fast_edge") edge = .1e-9;
        else if (scenario == "low_core") { core_voltage = 1.08; sample_voltage = 3.63; }
        else if (scenario == "high_core") { core_voltage = 1.32; sample_voltage = 2.97; }
        else if (scenario != "nominal" && scenario != "missing" && scenario != "reclear")
            throw std::runtime_error("unknown scenario");
        constexpr double start = 24e-9, stop = 160e-9;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.005e-12);
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        electrical ground, source, core;
        sim.reference(ground);
        source = sim_pwl_voltage_source(ground, {{0, 0}, {1e-9, 0}, {5e-9, core_voltage}});
        core = resistor(source, resistance(10));
        core = capacitor(ground, capacitance_value(20e-12));
        one_g::ihp_sg13g2::receive_aperture_sensor dut;
        dut.Vdd = core;
        dut.Vss = ground;
        using wave = std::vector<std::pair<double, double>>;
        wave ref{{0, 0}}, sample{{0, 0}};
        wave clear{{0, 0}, {1e-9, 0}, {5e-9, core_voltage}, {16e-9, core_voltage}, {16.4e-9, 0}};
        for (double t = start; t < stop - 4e-9; t += 8e-9) {
            ref.emplace_back(t, 0); ref.emplace_back(t + .4e-9, core_voltage);
            ref.emplace_back(t + 4e-9, core_voltage); ref.emplace_back(t + 4.4e-9, 0);
        }
        for (double t = start + phase; t < stop - 4e-9; t += feedback_period) {
            if (scenario == "missing" && t >= 80e-9) break;
            sample.emplace_back(t, 0); sample.emplace_back(t + edge, sample_voltage);
            sample.emplace_back(t + 1.5e-9, sample_voltage); sample.emplace_back(t + 1.5e-9 + edge, 0);
        }
        if (scenario == "reclear") {
            clear.insert(clear.end(), {{79e-9, 0}, {79.4e-9, core_voltage},
                                      {85e-9, core_voltage}, {85.4e-9, 0}});
        }
        auto drive = [&](port<electrical>& target, const wave& points) {
            electrical driver;
            driver = sim_pwl_voltage_source(ground, points);
            target = resistor(driver, resistance(50));
        };
        drive(dut.REF, ref); drive(dut.SAMPLE_N, sample); drive(dut.CLEAR, clear);
        dut.elaborate();
        for (const auto& item : std::initializer_list<std::pair<std::string, electrical>>{
                {"VLOGIC", core}, {"REF", dut.REF}, {"SAMPLE_N", dut.SAMPLE_N}, {"CLEAR", dut.CLEAR},
                {"REF_INPUT", dut.REF_INPUT}, {"SAMPLE_INPUT", dut.SAMPLE_INPUT},
                {"REF_CLOCK", dut.REF_CLOCK}, {"SAMPLE_CLOCK", dut.SAMPLE_CLOCK},
                {"REF_EDGE", dut.REF_EDGE}, {"SAMPLE_EDGE", dut.SAMPLE_EDGE},
                {"REF_CLEAR", dut.REF_CLEAR}, {"CLEAR_DELAY", dut.CLEAR_DELAY},
                {"UP", dut.UP}, {"DOWN", dut.DOWN}})
            sim.observe(item.first, item.second, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(2e-12), duration(2e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(4e-9), duration(2e-12));
            for (unsigned k = 1; k < 40; ++k)
                sim.advance_to(duration((k + 1) * 4e-9), duration(2e-12));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
