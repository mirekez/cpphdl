#include "../core_receive_dac.hpp"
#include <VerilogA/experiment.hpp>
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
        constexpr double warmup = 8e-6, period = 4e-9, step = 2e-12;
        constexpr double switch_step = .05e-12, switch_window = 1.5e-9;
        // Full-bank switching, each bit alone, alternating bits and the
        // major-carry boundary stress the shared bias and the actual ladder.
        constexpr unsigned codes[]{0, 255, 0, 128, 64, 32, 16, 8, 4, 2, 1, 85, 170, 127, 128, 255};
        constexpr double stop = warmup + std::size(codes) * period;
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(.005e-12);
        electrical ground, supply, logic_source, vdd, core;
        sim.reference(ground);
        supply = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 3.3}});
        logic_source = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 1.2}});
        vdd = resistor(supply, resistance(.25));
        core = resistor(logic_source, resistance(10));
        vdd = capacitor(ground, capacitance_value(20e-12));
        core = capacitor(ground, capacitance_value(20e-12));
        one_g::ihp_sg13g2::core_receive_dac dut;
        dut.Vdd = vdd;
        dut.Vlogic = core;
        dut.Vss = ground;
        for (unsigned bit = 0; bit < 8; ++bit) {
            std::vector<std::pair<double, double>> wave{{0, 0}};
            double previous = 0;
            for (unsigned index = 0; index < std::size(codes); ++index) {
                const double value = ((codes[index] >> bit) & 1) ? 1.2 : 0;
                const double time = warmup + index * period;
                wave.emplace_back(time, previous);
                wave.emplace_back(time + .1e-9, value);
                previous = value;
            }
            electrical source;
            source = sim_pwl_voltage_source(ground, wave);
            dut.TRIAL[bit] = resistor(source, resistance(50));
        }
        dut.OUT = capacitor(ground, capacitance_value(100e-15));
        dut.elaborate();
        sim.observe("OUT", dut.OUT, ground);
        sim.observe("VDD", vdd, ground);
        sim.observe("VLOGIC", core, ground);
        sim.observe("BIAS", dut.translation_bias.BIAS, ground);
        for (unsigned bit = 0; bit < 8; ++bit) {
            sim.observe("TRIAL" + std::to_string(bit), dut.TRIAL[bit], ground);
            sim.observe("BIT_N" + std::to_string(bit), dut.ladder.BIT_N[bit], ground);
            sim.observe("COMPLEMENT" + std::to_string(bit), dut.controls[bit].complement, ground);
            sim.observe("LOW" + std::to_string(bit), dut.controls[bit].low_node, ground);
            sim.observe("LOW_N" + std::to_string(bit), dut.controls[bit].low_node_n, ground);
        }
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination,
            {duration(stop), duration(step), duration(.5e-12), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(duration(warmup), duration(1e-9));
            sim.discard_trace();
            // Regeneration amplifies backward-Euler timing error. Refine the
            // switching interval without wasting fine steps on settled nodes.
            for (unsigned index = 0; index < std::size(codes); ++index) {
                sim.advance_to(duration(warmup + index * period + switch_window), duration(switch_step));
                sim.advance_to(duration(warmup + (index + 1) * period), duration(step));
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
