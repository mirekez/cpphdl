#include <basic/cmos_storage.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

template <class Storage>
int experiment(const std::filesystem::path& destination, bool native, bool loaded) {
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(1e-13);
    electrical supply, vdd, vss, d, clock, clear;
    sim.reference(vss);
    supply = sim_voltage_source(vss, voltage(1.2));
    vdd = resistor(supply, resistance(10));
    vdd = capacitor(vss, capacitance_value(2e-12));
    std::vector<std::pair<double, double>> clocks{{0, 0}}, data{{0, 0}};
    double previous = 0;
    for (unsigned half = 1; half < 192; ++half) {
        const double time = half * 2e-9;
        clocks.push_back({time, previous});
        previous = half & 1 ? 1.2 : 0;
        clocks.push_back({time + .1e-9, previous});
    }
    previous = 0;
    for (unsigned index = 0; index < 192; ++index) {
        const double time = index * 2e-9 + .8e-9;
        data.push_back({time, previous});
        previous = ((index * 73 + index / 3) % 17) < 8 ? 1.2 : 0;
        data.push_back({time + .1e-9, previous});
    }
    d = sim_pwl_voltage_source(vss, data);
    clock = sim_pwl_voltage_source(vss, clocks);
    clear = sim_pwl_voltage_source(vss, {{0, 1.2}, {12e-9, 1.2}, {12.1e-9, 0},
        {139e-9, 0}, {139.1e-9, 1.2}, {145e-9, 1.2}, {145.1e-9, 0}});
    Storage component;
    if constexpr (std::is_same_v<Storage, cmos_d_latch>)
        component.ENABLE = resistor(clock, resistance(50));
    else
        component.CLOCK = resistor(clock, resistance(50));
    if constexpr (!std::is_same_v<Storage, cmos_divide2>)
        component.D = resistor(d, resistance(50));
    component.CLEAR = resistor(clear, resistance(50));
    component.Vdd = vdd;
    component.Vss = vss;
    component.elaborate();
    if (loaded) {
        component.Q = capacitor(vss, capacitance_value(.1e-12));
        component.Q = resistor(vss, resistance(10000));
    }
    sim.observe("Q", component.Q, vss);
    sim.observe("Q_N", component.Q_N, vss);
    sim.observe("D", d, vss);
    sim.observe("CLOCK", clock, vss);
    sim.observe("CLEAR", clear, vss);
    sim.observe("VDD", vdd, vss);
    std::filesystem::create_directories(destination);
    const duration stop(384e-9), step(4e-9 / 128);
    verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
    if (native) {
        sim.run(stop, step);
        sim.write(destination / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::runtime_error("expected destination run|export latch|dff|divider loaded");
        const std::string_view flow(argv[2]), kind(argv[3]);
        if (flow != "run" && flow != "export")
            throw std::runtime_error("use run or export");
        if (kind == "latch")
            return experiment<cmos_d_latch>(argv[1], flow == "run", std::stoi(argv[4]) != 0);
        if (kind == "dff")
            return experiment<cmos_dff>(argv[1], flow == "run", std::stoi(argv[4]) != 0);
        if (kind == "divider")
            return experiment<cmos_divide2>(argv[1], flow == "run", std::stoi(argv[4]) != 0);
        throw std::runtime_error("unknown storage circuit");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
