#pragma once
#include "export.hpp"

namespace analog::verilog_a {

// A simulation bundle exposes all physical nodes to SPICE for observations and initial conditions.
// Its ideal sources/drivers are testbench models, not synthesized circuit devices.
inline void write_experiment(const simulation& sim, const std::filesystem::path& destination, transient analysis) {
    detail::validate(sim, true);
    positive(analysis.stop.value, "experiment stop");
    positive(analysis.step.value, "experiment output step");
    positive(analysis.maximum_step.value, "experiment maximum SPICE step");
    auto nodes = detail::nodes(sim);
    if (!nodes.contains(0))
        detail::fail("experiment needs a reference-connected physical network");
    for (const auto& observation : sim.observations()) {
        if (observation.digital || !nodes.contains(sim.root(observation.a)) || !nodes.contains(sim.root(observation.b)))
            detail::fail("experiment observations must refer to connected electrical terminals");
    }
    std::vector<detail::node_port> ports;
    for (auto id : nodes)
        ports.push_back({"n" + std::to_string(id), id});
    auto verilog = detail::module(sim, "AnalogExperiment", ports, true);
    auto n = [&](std::size_t id) { return sim.root(id) == 0 ? std::string("0") : "n" + std::to_string(sim.root(id)); };
    std::map<std::size_t, double> initial{{0, 0}};
    for (std::size_t pass = 0; pass <= sim.elements.size(); ++pass)
        for (const auto& e : sim.elements) {
            if (e.kind != law::capacitor && e.kind != law::sim_voltage_source)
                continue;
            auto a = sim.root(e.a), b = sim.root(e.b);
            double value = e.kind == law::capacitor ? e.initial : e.stepped && e.at == 0 ? e.next : e.value;
            if (initial.contains(a) && initial.contains(b)) {
                if (std::abs(initial.at(a) - initial.at(b) - value) > 1e-9)
                    detail::fail("inconsistent initial voltage constraints");
            } else if (initial.contains(b))
                initial[a] = initial.at(b) + value;
            else if (initial.contains(a))
                initial[b] = initial.at(a) - value;
        }
    for (const auto& seed : sim.elements) {
        if ((seed.kind != law::capacitor && seed.kind != law::sim_voltage_source) || initial.contains(sim.root(seed.a)))
            continue;
        initial[sim.root(seed.b)] = 0; // Free common-mode guess; only branch charge is a physical initial state.
        for (std::size_t pass = 0; pass <= sim.elements.size(); ++pass)
            for (const auto& e : sim.elements) {
                if (e.kind != law::capacitor && e.kind != law::sim_voltage_source)
                    continue;
                auto a = sim.root(e.a), b = sim.root(e.b);
                double value = e.kind == law::capacitor ? e.initial : e.stepped && e.at == 0 ? e.next : e.value;
                if (initial.contains(a) && initial.contains(b)) {
                    if (std::abs(initial.at(a) - initial.at(b) - value) > 1e-9)
                        detail::fail("inconsistent initial voltage constraints");
                } else if (initial.contains(b))
                    initial[a] = initial.at(b) + value;
                else if (initial.contains(a))
                    initial[b] = initial.at(a) - value;
            }
    }
    std::set<std::size_t> driver_bits;
    for (const auto& e : sim.elements)
        if (e.kind == law::sim_driver)
            driver_bits.insert(e.bit);
    std::ostringstream deck;
    deck << "C++HDL Verilog-A independent transient experiment\n"
         << "* Sources, initial charges and observations come from the same elaborated C++ bench.\n"
         << ".model CircuitModel AnalogExperiment\nNdesign";
    for (const auto& p : ports)
        deck << ' ' << n(p.id);
    for (auto bit : driver_bits)
        deck << " bit" << bit;
    deck << " CircuitModel\n";
    for (std::size_t i = 0; i < sim.elements.size(); ++i) {
        const auto& e = sim.elements[i];
        if (e.kind != law::sim_voltage_source)
            continue;
        deck << "Vsource" << i << ' ' << n(e.a) << ' ' << n(e.b);
        if (!e.stepped || e.at == 0 || e.at >= analysis.stop.value)
            deck << " DC " << detail::number(e.stepped && e.at == 0 ? e.next : e.value);
        else
            // Duplicate time points represent the same ideal discontinuity as the C++ stimulus.
            deck << " PWL(0 " << detail::number(e.value) << ' ' << detail::number(e.at) << ' '
                 << detail::number(e.value) << ' ' << detail::number(e.at) << ' ' << detail::number(e.next) << ' '
                 << detail::number(analysis.stop.value) << ' ' << detail::number(e.next) << ')';
        deck << '\n';
    }
    // ngspice PULSE uses a finite edge. Keep it explicit and much shorter than either integration step.
    double edge = std::min(analysis.maximum_step.value, analysis.step.value) * 1e-4;
    for (const auto& input : sim.inputs)
        if (driver_bits.contains(input.out) && input.kind == digital_stimulus::clock)
            edge = std::min(edge, input.period / 10000);
    for (auto bit : driver_bits) {
        const digital_stimulus* stimulus = nullptr;
        for (const auto& input : sim.inputs)
            if (input.out == bit)
                stimulus = &input;
        if (!stimulus)
            detail::fail("sim_driver needs a directly declared constant/step/clock bench stimulus");
        deck << "Vbit" << bit << " bit" << bit << " 0 ";
        if (stimulus->kind == digital_stimulus::constant)
            deck << "DC " << stimulus->before;
        else if (stimulus->kind == digital_stimulus::clock)
            deck << "PULSE(0 1 " << detail::number(stimulus->period / 2) << ' ' << detail::number(edge) << ' '
                 << detail::number(edge) << ' ' << detail::number(stimulus->period / 2 - edge) << ' '
                 << detail::number(stimulus->period) << ')';
        else
            deck << "PWL(0 " << stimulus->before << ' ' << detail::number(stimulus->at) << ' ' << stimulus->before
                 << ' ' << detail::number(stimulus->at) << ' ' << stimulus->after << ' '
                 << detail::number(analysis.stop.value) << ' ' << stimulus->after << ')';
        deck << '\n';
    }
    for (const auto& [id, value] : initial)
        if (id != 0)
            deck << ".ic V(" << n(id) << ")=" << detail::number(value) << '\n';
    deck << ".options method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 trtol=7\n"
         << ".control\npre_osdi design.osdi\nset noaskquit\nset wr_singlescale\nset wr_vecnames\n"
         << "set numdgt=17\ntran " << detail::number(analysis.step.value) << ' ' << detail::number(analysis.stop.value)
         << " 0 " << detail::number(analysis.maximum_step.value) << " uic\n";
    std::size_t column = 0;
    std::ostringstream columns;
    columns << "time_s";
    for (const auto& observation : sim.observations()) {
        if (observation.digital)
            detail::fail("Verilog-A waveform output supports electrical observations only");
        deck << "let observation" << column << " = ";
        if (sim.root(observation.a) == sim.root(observation.b))
            deck << "0 * time";
        else if (sim.root(observation.a) == 0)
            deck << "-v(" << n(observation.b) << ')';
        else if (sim.root(observation.b) == 0)
            deck << "v(" << n(observation.a) << ')';
        else
            deck << "v(" << n(observation.a) << ") - v(" << n(observation.b) << ')';
        deck << '\n';
        columns << ',' << json_string(observation.name);
        ++column;
    }
    if (!column)
        detail::fail("experiment needs at least one electrical observation");
    deck << "wrdata spice.dat";
    for (std::size_t i = 0; i < column; ++i)
        deck << " observation" << i;
    deck << "\nquit\n.endc\n.end\n";
    std::filesystem::create_directories(destination);
    detail::write_file(destination / "design.va", verilog);
    detail::write_file(destination / "bench.cir", deck.str());
    // Observation names are preserved separately from ngspice's temporary vector identifiers.
    detail::write_file(destination / "columns.csv", columns.str() + '\n');
    std::ostringstream conditions;
    conditions << "{\n  \"stop_s\": " << detail::number(analysis.stop.value)
               << ",\n  \"cpp_step_s\": " << detail::number(analysis.step.value)
               << ",\n  \"spice_max_step_s\": " << detail::number(analysis.maximum_step.value)
               << ",\n  \"driver_edge_s\": " << detail::number(edge)
               << ",\n  \"initialization\": \"explicit capacitor/source voltages, SPICE UIC\",\n"
               << "  \"evidence\": \"selected-model trajectory comparison, not universal correctness proof\"\n}\n";
    detail::write_file(destination / "conditions.json", conditions.str());
}
} // namespace analog::verilog_a
