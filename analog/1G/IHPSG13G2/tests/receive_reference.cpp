#include "../receive_reference.hpp"
#include "receive_gain_process_devices.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4)
            throw std::runtime_error("expected destination run|export [wide|receiver]");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        if (mode != "run" && mode != "export")
            throw std::runtime_error("unknown reference experiment mode");
        const std::string load_profile = argc == 4 ? argv[3] : "wide";
        if (load_profile != "wide" && load_profile != "receiver")
            throw std::runtime_error("unknown reference load profile");
        constexpr double warmup = 16e-6, stop = warmup + 256e-9, step = 5e-12;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.1e-12);
        electrical ground, supply, rail, load;
        sim.reference(ground);
        supply = sim_voltage_source(ground, voltage(3.3));
        rail = resistor(supply, resistance(.25));
        rail = capacitor(ground, capacitance_value(20e-12));
        std::vector<std::pair<double, double>> wave{{0, 0}, {1e-6, 0}, {1.1e-6, load_profile == "receiver" ? 1.2 : 1.4}};
        // The receiver profile spans the PGA terminal voltages. The wider
        // characterization profile remains available to expose its limits.
        const std::array<double, 8> levels = load_profile == "receiver"
            ? std::array<double, 8>{.9, 1.2, 1.35, 1.5, .9, 1.5, 1.2, 1.35}
            : std::array<double, 8>{1.1, 1.4, 1.65, 1.9, 1.1, 1.9, 1.4, 1.65};
        for (unsigned i = 0; i < 8; ++i) {
            const double time = warmup + i * 32e-9;
            wave.emplace_back(time, wave.back().second);
            wave.emplace_back(time + .1e-9, levels[i]);
        }
        wave.emplace_back(stop, wave.back().second);
        load = sim_pwl_voltage_source(ground, wave);
#ifdef CPPHDL_ADC_RANGE_REFERENCE
        one_g::ihp_sg13g2::adc_range_reference reference;
        constexpr const char* profile = "adc_range";
#else
        one_g::ihp_sg13g2::receive_reference reference;
        constexpr const char* profile = "midrail";
#endif
        reference.Vdd = rail;
        reference.Vss = ground;
        reference.elaborate();
        // Eight 900-ohm strings are the static load of eight differential
        // PGAs. Their actual dynamic selector/input charge is tested later
        // in the integrated circuit, rather than invented in this unit load.
        reference.OUT = resistor(load, resistance(900.0 / 8));
        reference.OUT = capacitor(ground, capacitance_value(8 * 50e-15));
        sim.observe("OUT", reference.OUT, ground);
        sim.observe("VDD", rail, ground);
        sim.observe("TARGET", reference.reference.TAP, ground);
        sim.observe("DRIVE", reference.buffer.amplifier.OUT, ground);
        sim.observe("LOAD", load, ground);
        sim.observe("BIAS_N", reference.n_bias.TAP, ground);
        sim.observe("BIAS_P", reference.p_bias.TAP, ground);
        sim.observe("TAIL_BIAS", reference.TAIL_BIAS, ground);
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(step), duration(step), 1e-16, 2, 1e-18});
        write_process_devices(sim, rail, destination);
        std::ofstream profile_file(destination / "reference_profile.json");
        profile_file << "{\"profile\":\"" << profile << "\",\"load_profile\":\"" << load_profile << "\"}\n";
        profile_file.close();
        if (!profile_file)
            throw std::runtime_error("cannot write reference profile");
        if (mode == "run") {
            sim.run(duration(warmup), duration(1e-9));
            sim.discard_trace();
            sim.advance_to(duration(stop), duration(step));
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
