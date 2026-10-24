#pragma once
#include "../IHPSG13G2/core_transmit_driver.hpp"
#include <VerilogA/experiment.hpp>
#include <sparse.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace analog;

// Replay the separately checked real controller trace at the core boundary.
// Every downstream voltage, gate charge and output current is solved here.
template <class Driver, bool Programmable = false>
int run_core_transmit_driver(int argc, char** argv) {
    try {
        if (argc != (Programmable ? 7 : 6))
            throw std::runtime_error("expected work run|export controls.csv load_ohm refined [trim_code]");
        const std::filesystem::path work(argv[1]);
        const std::string mode(argv[2]);
        const double load = std::stod(argv[4]);
        const bool refined = std::stoi(argv[5]) != 0;
        const unsigned trim = Programmable ? unsigned(std::stoul(argv[6])) : 0;
        if (trim > 63)
            throw std::runtime_error("trim code exceeds six physical branches");
        if (!std::isfinite(load) || load <= 0 || (mode != "run" && mode != "export"))
            throw std::runtime_error("invalid core driver experiment");
        std::ifstream input(argv[3]);
        std::string line;
        std::getline(input, line);
        if (line != "index,code,previous,enable,reset,valid,fault,main_p,main_n,history_p,history_n")
            throw std::runtime_error("invalid controller trace interface");
        std::vector<std::array<unsigned, 4>> masks;
        while (std::getline(input, line)) {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            std::array<unsigned, 11> fields{};
            for (auto& field : fields)
                if (!(row >> field))
                    throw std::runtime_error("incomplete controller trace");
            if (fields[0] != masks.size())
                throw std::runtime_error("nonconsecutive controller trace");
            masks.push_back({fields[7], fields[8], fields[9], fields[10]});
            for (unsigned value : masks.back())
                if (value > 15)
                    throw std::runtime_error("controller mask exceeds four gates");
        }
        if (masks.size() < 100)
            throw std::runtime_error("insufficient controller trace");
        constexpr double ui = 8e-9, startup = 512e-9;
        const double stop = startup + masks.size() * ui;
        const double step = ui / (refined ? 1024 : 512);
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(ui / 65536);
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        electrical ground, source, supply, logic_source, logic_supply;
        sim.reference(ground);
        source = sim_voltage_source(ground, voltage(3.3));
        supply = resistor(source, resistance(.25));
        supply = capacitor(ground, capacitance_value(20e-12));
        logic_source = sim_voltage_source(ground, voltage(1.2));
        logic_supply = resistor(logic_source, resistance(2));
        logic_supply = capacitor(ground, capacitance_value(20e-12));
        Driver driver;
        driver.Vdd = supply;
        driver.Vlogic = logic_supply;
        driver.Vss = ground;
        if constexpr (Programmable)
            for (unsigned bit = 0; bit < 6; ++bit) {
                electrical command;
                command = sim_voltage_source(ground, voltage((trim & (1u << bit)) ? 0 : 1.2));
                driver.TRIM_N[bit] = resistor(command, resistance(50));
            }
        driver.elaborate();
        driver.P = resistor(driver.N, resistance(load));
        for (unsigned bank = 0; bank < 4; ++bank)
            for (unsigned bit = 0; bit < 4; ++bit) {
                std::vector<std::pair<double, double>> wave{{0, 0}};
                bool previous = false;
                for (unsigned index = 0; index < masks.size(); ++index) {
                    const bool value = (masks[index][bank] >> bit) & 1;
                    if (value != previous) {
                        const double time = startup + (index + .15) * ui;
                        wave.push_back({time, previous ? 1.2 : 0});
                        wave.push_back({time + .1e-9, value ? 1.2 : 0});
                        previous = value;
                    }
                }
                electrical command;
                command = sim_pwl_voltage_source(ground, wave);
                auto& terminal = bank == 0 ? driver.MAIN_P[bit] : bank == 1 ? driver.MAIN_N[bit]
                               : bank == 2 ? driver.HISTORY_P[bit] : driver.HISTORY_N[bit];
                terminal = resistor(command, resistance(50));
                sim.observe("G" + std::to_string(bank) + "_" + std::to_string(bit),
                            driver.gates[4 * bank + bit].Y, ground);
            }
        sim.observe("P", driver.P, ground);
        sim.observe("N", driver.N, ground);
        sim.observe("DIFF", driver.P, driver.N);
        sim.observe("BIAS", driver.output.bias, ground);
        sim.observe("VDD", supply, ground);
        sim.observe("SUPPLY_DROP", source, supply);
        sim.observe("TRANSLATION_BIAS", driver.translation_bias.BIAS, ground);
        sim.observe("VLOGIC", logic_supply, ground);
        sim.observe("LOGIC_DROP", logic_source, logic_supply);
        for (unsigned i = 0; i < 16; ++i)
            sim.observe("CORE" + std::to_string(i), driver.gates[i].A, ground);
        if constexpr (Programmable)
            for (unsigned bit = 0; bit < 6; ++bit) {
                sim.observe("TRIM_CORE" + std::to_string(bit), driver.TRIM_N[bit], ground);
                sim.observe("TRIM_HIGH" + std::to_string(bit), driver.trim_gates[bit].Y, ground);
            }
        std::filesystem::create_directories(work);
        std::ofstream(work / "interface.json") << std::setprecision(17)
            << "{\"ui_s\":" << ui << ",\"startup_s\":" << startup << ",\"stop_s\":" << stop
            << ",\"step_s\":" << step << ",\"load_ohm\":" << load
            << ",\"filtered\":true,\"warm_words\":34"
            << (Programmable ? ",\"trim_code\":" + std::to_string(trim) : "") << "}\n";
        verilog_a::write_experiment(sim, work, {duration(stop), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        if (mode == "export")
            return 0;
        std::ofstream trace(work / "trace.csv");
        trace << "time_s";
        for (const auto& observation : sim.observations())
            trace << ',' << observation.name;
        trace << '\n' << std::setprecision(17);
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
                        throw std::runtime_error("unknown core driver observation");
                    trace << ',' << *value;
                }
                trace << '\n';
            }
            sim.discard_trace();
            current = next;
        }
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
