#include "../wideband_receive_frontend.hpp"
template <std::size_t Count>
using ReceiveFrontend = analog::one_g::ihp_sg13g2::wideband_receive_frontend<Count>;
#include <sparse.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

template <std::size_t Count>
int experiment(const std::filesystem::path& destination, bool native, double load, double step) {
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(.05e-12);
    use_sparse_solver(sim, sparse_strategy::certified_lu);
    electrical vss, source, vdd, core_source, core, plus_source, minus_source;
    sim.reference(vss);
    source = sim_voltage_source(vss, voltage(3.3));
    vdd = resistor(source, resistance(.25));
    vdd = capacitor(vss, capacitance_value(20e-12), voltage(3.3));
    core_source = sim_voltage_source(vss, voltage(1.2));
    core = resistor(core_source, resistance(10));
    core = capacitor(vss, capacitance_value(20e-12));
    using wave = std::vector<std::pair<double, double>>;
    wave plus{{0, 1.65}}, minus{{0, 1.65}};
    auto change = [](wave& points, double time, double value) {
        if (points.back().second != value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + .1e-9, value);
        }
    };
    // Two acquisitions per slice expose held-capacitor reuse and kickback.
    for (unsigned index = 0; index < 2 * Count; ++index) {
        const double differential = .12 * (int((index * 3) % 5) - 2);
        change(plus, (8 + 4 * index) * 1e-9, 1.65 + differential / 2);
        change(minus, (8 + 4 * index) * 1e-9, 1.65 - differential / 2);
    }
    plus_source = sim_pwl_voltage_source(vss, plus);
    minus_source = sim_pwl_voltage_source(vss, minus);
    ReceiveFrontend<Count> receiver;
    receiver.PLUS = resistor(plus_source, resistance(50));
    receiver.MINUS = resistor(minus_source, resistance(50));
    receiver.Vdd = vdd;
    receiver.Vss = vss;
    receiver.Vlogic = core;
    receiver.EVALUATE = sim_voltage_source(vss, voltage(0));
    receiver.CLEAR = sim_pwl_voltage_source(vss, {{0, 3.3}, {2e-9, 3.3}, {2.1e-9, 0}});
    for (unsigned slice = 0; slice < Count; ++slice) {
        wave acquire{{0, 0}};
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            const double time = (8 + 4 * (slice + repeat * Count)) * 1e-9;
            change(acquire, time + 1e-9, 3.3);
            change(acquire, time + 3e-9, 0);
        }
        receiver.TRACK[slice] = sim_pwl_voltage_source(vss, acquire);
        for (unsigned bit = 0; bit < 8; ++bit)
            receiver.BIT_N[slice][bit] = vdd;
    }
    receiver.elaborate();
    receiver.adc.IN = capacitor(vss, capacitance_value(load), voltage(1.65));
    sim.observe("PLUS", receiver.PLUS, vss);
    sim.observe("MINUS", receiver.MINUS, vss);
    sim.observe("DRAIN", receiver.amplifier.OUTP, vss);
    sim.observe("BUFFER", receiver.adc.IN, vss);
    sim.observe("VDD", vdd, vss);
    sim.observe("VLOGIC", core, vss);
    for (unsigned slice = 0; slice < Count; ++slice)
        sim.observe("HELD" + std::to_string(slice), receiver.adc.slices[slice].sampler.OUT, vss);
    const duration stop((12 + 8 * Count) * 1e-9), timestep(step);
    std::filesystem::create_directories(destination);
    verilog_a::write_experiment(sim, destination,
        {stop, timestep, duration(step / 2), 1e-16, 2, 1e-18});
    if (native) {
        sim.run(stop, timestep);
        sim.write(destination / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc != 6)
            throw std::runtime_error("expected destination run|export slices load_pF step_ps");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned count = unsigned(std::stoul(argv[3]));
        const double load = std::stod(argv[4]) * 1e-12, step = std::stod(argv[5]) * 1e-12;
        if ((mode != "run" && mode != "export") || load <= 0 || step <= 0 || step > 10e-12)
            throw std::runtime_error("invalid receive-front-end experiment");
        if (count == 1)
            return experiment<1>(destination, mode == "run", load, step);
        if (count == 4)
            return experiment<4>(destination, mode == "run", load, step);
        if (count == 16)
            return experiment<16>(destination, mode == "run", load, step);
        throw std::runtime_error("use 1, 4 or 16 slices");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
