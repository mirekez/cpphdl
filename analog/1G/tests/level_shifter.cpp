#include <basic/level_shifter.hpp>
#include <basic/cascoded_level_shifter.hpp>
#include "../clock_level_shifter.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>
#include <memory>
#include <type_traits>

using namespace analog;

template <class Shift>
int experiment(const std::filesystem::path& destination, std::string_view mode, double low, double high,
               bool loaded, double frequency) {
    constexpr bool cascoded = std::is_base_of_v<cascoded_level_shifter, Shift>;
    const double startup = cascoded ? 32e-9 : 0;
    if (frequency != 62.5 && frequency != 250)
        throw std::runtime_error("supported level-shifter rates are 62.5 or 250 MHz");
    const double half_period = .5e-6 / frequency;
    simulation sim;
    sim.analytic_jacobian = true;
    electrical source, logic_source, vdd, vl, vss, command;
    sim.reference(vss);
    source = sim_voltage_source(vss, voltage(high));
    logic_source = sim_voltage_source(vss, voltage(low));
    vdd = resistor(source, resistance(10));
    vdd = capacitor(vss, capacitance_value(2e-12));
    vl = resistor(logic_source, resistance(10));
    vl = capacitor(vss, capacitance_value(.5e-12));
    std::vector<std::pair<double, double>> wave{{0, 0}};
    double previous = 0;
    for (unsigned i = 1; i < 16; ++i) {
        const double next = (i & 1) ? low : 0;
        wave.push_back({startup + i * half_period, previous});
        wave.push_back({startup + i * half_period + .1e-9, next});
        previous = next;
    }
    command = sim_pwl_voltage_source(vss, wave);
    Shift shift;
    shift.A = resistor(command, resistance(50));
    shift.Vlogic = vl;
    shift.Vdd = vdd;
    shift.Vss = vss;
    std::unique_ptr<level_shift_bias> bias;
    if constexpr (cascoded) {
        bias = std::make_unique<level_shift_bias>();
        bias->Vdd = vdd;
        bias->Vss = vss;
        bias->Vlogic = vl;
        bias->BIAS = shift.BIAS;
        bias->elaborate();
    }
    shift.elaborate();
    if (loaded) {
        shift.Y = capacitor(vss, capacitance_value(.2e-12));
        shift.Y = resistor(vss, resistance(10000));
    }
    sim.observe("Y", shift.Y, vss);
    sim.observe("Y_N", shift.Y_N, vss);
    sim.observe("A", shift.A, vss);
    sim.observe("VDD", vdd, vss);
    sim.observe("VLOGIC", vl, vss);
    sim.observe("COMMAND", command, vss);
    std::filesystem::create_directories(destination);
    const duration stop(startup + 16 * half_period);
    const duration step(std::max(8e-9 / 6144, stop.value / 99000));
    verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
    if (mode == "run") {
        sim.run(stop, step);
        sim.write(destination / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    if (mode != "export")
        throw std::runtime_error("use run or export");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 6 || argc > 8)
        return 2;
    try {
        const double low = std::stod(argv[3]), high = std::stod(argv[4]);
        const bool loaded = std::stoi(argv[5]) != 0;
        const double frequency = argc >= 7 ? std::stod(argv[6]) : 62.5;
        const std::string_view profile = argc == 8 ? argv[7] : "direct";
        if (profile == "cascoded")
            return experiment<one_g::clock_level_shifter>(argv[1], argv[2], low, high, loaded, frequency);
        if (profile != "direct")
            throw std::runtime_error("use direct or cascoded topology");
        return experiment<level_shifter>(argv[1], argv[2], low, high, loaded, frequency);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
