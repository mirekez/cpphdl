#include <cpphdl_veriloga.h>
#include <VerilogA/experiment.hpp>
#include <basic/basic.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace analog;
using namespace analog::literals;

int main() {
    int failures = 0;
    auto check = [&](bool pass, const char* name) {
        if (!pass) {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    auto rejects = [&](auto action, const char* name) {
        bool rejected = false;
        try {
            action();
        } catch (const diagnostic& error) {
            rejected = error.code == "E_VERILOGA";
        }
        check(rejected, name);
    };
    {
        simulation sim;
        cmos<> inverter;
        rejects([&] { (void)verilog_a::render_module(sim, "Inverter", {{"A", inverter.A}}); },
                "unelaborated IC cannot export an empty model");
        inverter.elaborate();
        auto text = verilog_a::render_module(
            sim, "Inverter", {{"A", inverter.A}, {"Y", inverter.Y}, {"Vdd", inverter.Vdd}, {"Vss", inverter.Vss}});
        check(text.find("cpphdl_channel") != std::string::npos && text.find("ddt") == std::string::npos,
              "MOS topology exports physical channels without invented storage");
        rejects([&] { (void)verilog_a::render_module(sim, "module", {{"A", inverter.A}}); }, "reserved module name");
        rejects([&] { (void)verilog_a::render_module(sim, "Inverter", {{"A", inverter.A}, {"A", inverter.Y}}); },
                "duplicate port names");
        rejects([&] { (void)verilog_a::render_module(sim, "Inverter", {}); }, "missing interface");
    }
    {
        simulation sim;
        electrical A, B;
        A = B;
        auto text = verilog_a::render_module(sim, "Wire", {{"A", A}, {"B", B}});
        check(text.find("V(B, A) <+ 0.0") != std::string::npos, "aliased ports remain a bidirectional short");
        simulation other;
        electrical foreign;
        bool context = false;
        try {
            (void)verilog_a::render_module(sim, "Wire", {{"A", foreign}});
        } catch (const diagnostic& error) {
            context = error.code == "E_CONTEXT";
        }
        check(context, "foreign ports rejected");
    }
    {
        simulation sim;
        ldo regulator;
        regulator.elaborate();
        auto text = verilog_a::render_module(
            sim, "LDO",
            {{"Vdd", regulator.Vdd}, {"Vss", regulator.Vss}, {"REF", regulator.REF}, {"OUT", regulator.OUT}});
        check(text.find("ddt(V") != std::string::npos && text.find("IC IC") != std::string::npos,
              "nested amplifier charge and component attribution survive flattening");
    }
    {
        simulation sim;
        cmos<fast_model> gate;
        gate.elaborate();
        rejects([&] { (void)verilog_a::render_module(sim, "FastGate", {{"A", gate.A}}); },
                "a simulation approximation cannot silently become hardware");
        auto text = verilog_a::render_simulation_model(
            sim, "FastGate", {{"A", gate.A}, {"Y", gate.Y}, {"Vdd", gate.Vdd}, {"Vss", gate.Vss}});
        check(text.find("SIMULATION APPROXIMATION") != std::string::npos &&
                  text.find("sim_controlled_resistor") != std::string::npos,
              "explicit behavioral export retains its controlled conductances and marks its fidelity");
    }
    {
        simulation sim;
        electrical reference, signal;
        sim.reference(reference);
        signal = sim_voltage_source(reference, 1_V);
        rejects([&] { (void)verilog_a::render_module(sim, "Source", {{"Y", signal}}); },
                "ideal bench source rejected in a reusable IC");
        rejects([&] { (void)verilog_a::render_simulation_model(sim, "Source", {{"Y", signal}}); },
                "behavioral IC export cannot silently drop a bench source");
    }
    {
        simulation sim;
        electrical ground, supply, input, output;
        bits<1> control;
        sim.reference(ground);
        supply = sim_voltage_source(ground, 1.2_V);
        input = sim_voltage_source(ground, 0_V);
        control = sim_sense(input, ground, .35_V, .85_V);
        output = sim_driver(control, supply, ground, 100_Ohm);
        rejects([&] { verilog_a::write_experiment(sim, "unused", {1_us, 10_ns, 10_ns}); },
                "full experiment cannot discard an analog-to-digital bridge");
        rejects([&] { (void)verilog_a::render_analog_partition(sim, "Partition", {{"Vss", ground}}); },
                "partition cannot hide terminals of omitted sources");
        auto text = verilog_a::render_analog_partition(
            sim, "Partition", {{"Vss", ground}, {"Vdd", supply}, {"IN", input}, {"Y", output}});
        check(text.find("ANALOG PARTITION ONLY") != std::string::npos &&
                  text.find("cpphdl_bit" + std::to_string(sim.elements.back().bit)) != std::string::npos &&
                  text.find("sim_driver") != std::string::npos,
              "explicit partition exposes external digital control and retains physical drive resistance");
    }
    {
        simulation sim;
        electrical ground, supply, output;
        bits<1> clock;
        sim.reference(ground);
        supply = sim_voltage_source(ground, 1.2_V);
        sim.clock(clock, 4_ns);
        output = sim_driver(clock, supply, ground, 100_Ohm);
        sim.observe("OUT", output, ground);
        const auto destination = std::filesystem::temp_directory_path() /
            ("cpphdl-export-edge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        verilog_a::transient timing{1_us, 10_ns, 10_ns};
        timing.driver_edge = duration(2e-15);
        for (double step : {1e-8, 1e-9}) {
            timing.step = duration(step);
            timing.maximum_step = duration(step);
            verilog_a::write_experiment(sim, destination, timing);
            std::ifstream source(destination / "conditions.json");
            const std::string text((std::istreambuf_iterator<char>(source)), {});
            const std::string key = "\"driver_edge_s\": ";
            const auto position = text.find(key);
            check(position != std::string::npos && std::stod(text.substr(position + key.size())) == 2e-15,
                "explicit experiment edge is independent of integration refinement");
        }
        for (double value : {-1e-12, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(), 2e-9}) {
            timing.driver_edge = duration(value);
            rejects([&] { verilog_a::write_experiment(sim, destination, timing); },
                "invalid experiment driver edge rejected");
        }
        std::filesystem::remove_all(destination);
    }
    {
        simulation sim;
        electrical physical;
        bits<1> input, output;
        sim.drive(input, 0);
        output = sim_invert(input);
        rejects([&] { (void)verilog_a::render_module(sim, "Digital", {{"P", physical}}); },
                "digital operations cannot be dropped from Verilog-A export");
    }
    return failures ? 1 : 0;
}
