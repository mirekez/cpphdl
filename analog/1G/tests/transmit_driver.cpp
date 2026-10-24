#include "../IHPSG13G2/emphasized_driver.hpp"
#include "transmit_stimulus.hpp"
#include <VerilogA/experiment.hpp>
#if __has_include(<Eigen/SparseLU>) || __has_include(<eigen3/Eigen/SparseLU>)
#include <sparse.hpp>
#define CPPHDL_TRANSMIT_SPARSE
#endif
#include <fstream>
#include <iomanip>
#include <iostream>

long _system_clock = -1;
using namespace analog;

// Preserve the elaborated connectivity for the separate PSP experiment. The
// geometry bindings are explicit design choices, not a fit inferred from beta.
void write_process_devices(const simulation& sim, const std::filesystem::path& work) {
    constexpr double tail_strength = one_g::ihp_sg13g2::emphasized_pam5_driver::nominal_tail_strength;
    auto node = [&](std::size_t id) {
        const auto root = sim.root(id);
        return root == 0 ? std::string("0") : "n" + std::to_string(root);
    };
    std::ofstream file(work / "process_devices.csv");
    file << "id,kind,a,b,gate,value,width_um,length_um\n" << std::setprecision(17);
    for (unsigned id = 0; id < sim.elements.size(); ++id) {
        const auto& e = sim.elements[id];
        if (e.kind == law::sim_voltage_source)
            continue;
        file << id << ',';
        if (e.kind == law::resistor || e.kind == law::capacitor) {
            file << (e.kind == law::resistor ? "R" : "C") << ',' << node(e.a) << ',' << node(e.b)
                 << ",0," << (e.kind == law::resistor ? e.r : e.c) << ",0,0\n";
        } else if (e.kind == law::nmos) {
            double width = 0;
            if (std::abs(e.beta - .02) < 1e-12 && std::abs(e.threshold - .7) < 1e-12)
                width = 100;
            else if (std::abs(e.beta - .014268727705112964) < 1e-12 && std::abs(e.threshold - .35) < 1e-12)
                width = 100;
            else if (std::abs(e.beta - tail_strength * 1.5) < 1e-12 && std::abs(e.threshold - .35) < 1e-12)
                width = 450;
            else if (std::abs(e.beta - tail_strength * .5) < 1e-12 && std::abs(e.threshold - .35) < 1e-12)
                width = 150;
            if (width == 0)
                throw std::runtime_error("missing explicit IHP MOS geometry binding");
            file << "N," << node(e.a) << ',' << node(e.b) << ',' << node(e.gate) << ",0," << width << ",0.45\n";
        } else {
            throw std::runtime_error("unsupported physical driver element in process export");
        }
    }
}


int main(int argc, char** argv) {
    if (argc != 6)
        return 2;
    try {
        const std::filesystem::path work(argv[1]);
        const std::string mode(argv[2]);
        const double load = std::stod(argv[3]); // Zero means an open differential load.
        const bool refined = std::stoi(argv[4]) != 0;
        const bool filtered = std::stoi(argv[5]) != 0;
        if (load < 0 || !std::isfinite(load))
            throw std::runtime_error("invalid differential output load");
        const auto words = stimulus();
        constexpr double ui = 8e-9, startup = 512e-9;
        const double stop = startup + words.size() * ui;
        const double step = ui / (refined ? 1024 : 512);
        simulation sim;
        sim.analytic_jacobian = true;
#ifdef CPPHDL_TRANSMIT_SPARSE
        use_sparse_solver(sim, sparse_strategy::certified_lu);
#endif
        sim.minimum_step = duration(ui / 65536);
        electrical ground, source, supply;
        sim.reference(ground);
        source = sim_voltage_source(ground, voltage(3.3));
        supply = resistor(source, resistance(.25));
        supply = capacitor(ground, capacitance_value(10e-12));
        one_g::ihp_sg13g2::emphasized_pam5_driver driver;
        driver.Vdd = supply;
        driver.Vss = ground;
        if (!filtered)
            driver.bias_filter_storage = capacitance_value(.5e-12);
        driver.elaborate();
        if (load > 0)
            driver.P = resistor(driver.N, resistance(load));
        for (unsigned bank = 0; bank < 4; ++bank)
            for (unsigned bit = 0; bit < 4; ++bit) {
                std::vector<std::pair<double, double>> wave{{0, 0}};
                bool previous = false;
                for (unsigned index = 0; index < words.size(); ++index) {
                    const auto& row = words[index];
                    const unsigned mask = bank == 0 ? row.main_p : bank == 1 ? row.main_n
                                        : bank == 2 ? row.history_p : row.history_n;
                    const bool value = (mask >> bit) & 1;
                    if (value != previous) {
                        const double time = startup + (index + .15) * ui;
                        wave.push_back({time, previous ? 3.3 : 0});
                        wave.push_back({time + .1e-9, value ? 3.3 : 0});
                        previous = value;
                    }
                }
                electrical command;
                command = sim_pwl_voltage_source(ground, wave);
                auto& terminal = bank == 0 ? driver.GATE_P[bit] : bank == 1 ? driver.GATE_N[bit]
                               : bank == 2 ? driver.HISTORY_P[bit] : driver.HISTORY_N[bit];
                terminal = resistor(command, resistance(50));
                sim.observe("G" + std::to_string(bank) + "_" + std::to_string(bit), terminal, ground);
            }
        sim.observe("P", driver.P, ground);
        sim.observe("N", driver.N, ground);
        sim.observe("DIFF", driver.P, driver.N);
        sim.observe("BIAS", driver.bias, ground);
        sim.observe("VDD", supply, ground);
        sim.observe("SUPPLY_DROP", source, supply);
        std::filesystem::create_directories(work);
        write_process_devices(sim, work);
        std::ofstream controls(work / "controls.csv");
        controls << "index,code,previous,enable,reset,valid,fault,main_p,main_n,history_p,history_n\n";
        for (unsigned i = 0; i < words.size(); ++i) {
            const auto& w = words[i];
            controls << i << ',' << w.code << ',' << w.previous << ',' << w.enable << ',' << w.reset << ','
                     << w.valid << ',' << w.fault << ',' << w.main_p << ',' << w.main_n << ','
                     << w.history_p << ',' << w.history_n << '\n';
        }
        controls.close();
        std::ofstream(work / "interface.json") << std::setprecision(17)
            << "{\"ui_s\":" << ui << ",\"startup_s\":" << startup << ",\"stop_s\":" << stop
            << ",\"step_s\":" << step << ",\"load_ohm\":" << load << ",\"filtered\":" << filtered
            << ",\"warm_words\":34}\n";
        verilog_a::write_experiment(sim, work, {duration(stop), duration(step), duration(step / 2), 1e-16, 2, 1e-18});
        if (mode == "export")
            return 0;
        if (mode != "run")
            throw std::runtime_error("use run or export");
        std::ofstream trace(work / "trace.csv");
        trace << "time_s";
        for (const auto& observation : sim.observations())
            trace << ',' << observation.name;
        trace << '\n' << std::setprecision(17);
        double current = 0;
        while (current < stop) {
            const double next = std::min(stop, current + 256e-9);
            if (current == 0)
                sim.run(duration(next), duration(step));
            else
                sim.advance_to(duration(next), duration(step));
            for (const auto& row : sim.trace) {
                trace << row.time;
                for (const auto& value : row.values) {
                    if (!value)
                        throw std::runtime_error("unknown physical driver observation");
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
