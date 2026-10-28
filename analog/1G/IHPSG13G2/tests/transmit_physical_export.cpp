#include "../cascoded_core_transmit_driver.hpp"
#include <VerilogA/export.hpp>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>

using namespace analog;

// Partition the elaborated TX, including its remaining storage. Every law
// belongs to one block; block ports retain the parent circuit's actual nodes.
// This is a connectivity export, not qualification of a complete TX layout.
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::ihp_sg13g2::cascoded_core_emphasized_pam5_driver tx;
        tx.elaborate();
        const auto original = sim.elements;
        std::vector<bool> claimed(original.size(), false);
        using verilog_a::named_port;
        std::vector<named_port> external{{"VDD", tx.Vdd}, {"VSS", tx.Vss},
            {"VLOGIC", tx.Vlogic}, {"P", tx.P}, {"N", tx.N}};
        for (unsigned bank = 0; bank < 4; ++bank)
            for (unsigned bit = 0; bit < 4; ++bit) {
                const std::string prefix = bank == 0 ? "MAIN_P" : bank == 1 ? "MAIN_N"
                    : bank == 2 ? "HISTORY_P" : "HISTORY_N";
                external.push_back({prefix + std::to_string(bit), bank == 0 ? tx.MAIN_P[bit]
                    : bank == 1 ? tx.MAIN_N[bit] : bank == 2 ? tx.HISTORY_P[bit] : tx.HISTORY_N[bit]});
            }
        for (unsigned bit = 0; bit < 6; ++bit)
            external.push_back({"TRIM_N" + std::to_string(bit), tx.TRIM_N[bit]});
        std::map<std::size_t, electrical> wires;
        for (const auto& port : external)
            wires.emplace(sim.root(port.terminal.id()), port.terminal);
        verilog_a::detail::write_file(destination / "transmitter.va",
            verilog_a::render_simulation_model(sim, "OneGPhysicalTransmit", external));
        std::ofstream manifest(destination / "partition.json");
        manifest << "{\n  \"elements\": " << original.size() << ",\n  \"ports\": {";
        for (std::size_t i = 0; i < external.size(); ++i)
            manifest << (i ? ", " : "") << '"' << external[i].name << "\": "
                     << sim.root(external[i].terminal.id());
        manifest << "},\n  \"blocks\": [\n";
        bool first = true;
        const auto block = [&](const std::string& name, const std::string& model,
                               const std::function<bool(const element&)>& select,
                               const std::vector<named_port>& ports) {
            std::set<std::size_t> boundary, inside, outside;
            for (const auto& port : ports) {
                boundary.insert(sim.root(port.terminal.id()));
                wires.emplace(sim.root(port.terminal.id()), port.terminal);
            }
            if (boundary.size() != ports.size())
                throw std::runtime_error("aliased TX block boundary: " + name);
            std::vector<std::size_t> identities;
            sim.elements.clear();
            for (std::size_t i = 0; i < original.size(); ++i) {
                const auto& element = original[i];
                const bool selected = select(element);
                for (const auto node : verilog_a::detail::terminals(element))
                    (selected ? inside : outside).insert(sim.root(node));
                if (selected) {
                    if (claimed[i])
                        throw std::runtime_error("TX law appears in two blocks: " + name);
                    claimed[i] = true;
                    identities.push_back(i);
                    sim.elements.push_back(element);
                }
            }
            if (identities.empty())
                throw std::runtime_error("empty TX physical block: " + name);
            for (const auto node : inside)
                if (outside.count(node) && !boundary.count(node))
                    throw std::runtime_error("undeclared TX block connection: " + name);
            for (const auto node : boundary)
                if (!inside.count(node))
                    throw std::runtime_error("unconnected TX block port: " + name);
            verilog_a::detail::write_file(destination / (name + ".va"),
                verilog_a::render_simulation_model(sim, model, ports));
            manifest << (first ? "" : ",\n") << "    {\"name\": \"" << name
                     << "\", \"model\": \"" << model << "\", \"ports\": {";
            first = false;
            for (std::size_t i = 0; i < ports.size(); ++i)
                manifest << (i ? ", " : "") << '"' << ports[i].name << "\": "
                         << sim.root(ports[i].terminal.id());
            manifest << "}, \"elements\": [";
            for (std::size_t i = 0; i < identities.size(); ++i)
                manifest << (i ? ", " : "") << identities[i];
            manifest << "]}";
        };
        for (unsigned index = 0; index < 22; ++index) {
            const auto& gate = index < 16 ? tx.gates[index] : tx.trim_gates[index - 16];
            const std::set<std::size_t> owners{gate.id(), gate.input_inverter.id(),
                gate.output_inverter[0].id(), gate.output_inverter[1].id()};
            block("translator_" + std::to_string(index), "OneGTransmitTranslator",
                [&](const auto& e) { return owners.count(e.component); },
                {{"VDD", gate.Vdd}, {"VSS", gate.Vss}, {"VLOGIC", gate.Vlogic},
                 {"BIAS", gate.BIAS}, {"A", gate.A}, {"Y", gate.Y}, {"Y_N", gate.Y_N}});
        }
        block("translation_bias", "OneGTransmitTranslationBias",
            [&](const auto& e) { return e.component == tx.translation_bias.id(); },
            {{"VDD", tx.translation_bias.Vdd}, {"VSS", tx.translation_bias.Vss},
             {"VLOGIC", tx.translation_bias.Vlogic}, {"BIAS", tx.translation_bias.BIAS}});
        auto& output = tx.output;
        std::vector<named_port> bias_ports{{"VDD", output.Vdd}, {"VSS", output.Vss},
            {"BIAS", output.bias}, {"CASCODE", output.CASCODE_BIAS}};
        for (unsigned bit = 0; bit < 6; ++bit)
            bias_ports.push_back({"TRIM_N" + std::to_string(bit), output.TRIM_N[bit]});
        block("current_bias", "OneGTransmitCurrentBias", [&](const auto& e) {
            return e.component == output.reference.id() || (e.component == output.id() &&
                e.kind == law::resistor && (sim.root(e.a) == sim.root(output.CASCODE_BIAS.id()) ||
                                           sim.root(e.b) == sim.root(output.CASCODE_BIAS.id())));
        }, bias_ports);
        for (unsigned bank = 0; bank < 2; ++bank)
            for (unsigned bit = 0; bit < 4; ++bit) {
                const auto tail = sim.root(output.TAIL[bank][bit].id());
                const auto lower = sim.root(output.LOWER[bank][bit].id());
                block((bank ? "history_" : "main_") + std::to_string(bit), "OneGTransmitCurrentCell",
                    [&](const auto& e) {
                        return e.component == output.id() && e.kind == law::nmos &&
                            (sim.root(e.a) == tail || sim.root(e.b) == tail ||
                             sim.root(e.a) == lower || sim.root(e.b) == lower);
                    }, {{"P", output.P}, {"N", output.N},
                        {"GP", bank ? output.HISTORY_P[bit] : output.GATE_P[bit]},
                        {"GN", bank ? output.HISTORY_N[bit] : output.GATE_N[bit]},
                        {"BIAS", output.bias}, {"CASCODE", output.CASCODE_BIAS}, {"VSS", output.Vss},
                        {"TAIL", output.TAIL[bank][bit]}, {"LOWER", output.LOWER[bank][bit]}});
            }
        // Keep every remaining declared passive. In particular, exposing the
        // current cells' internal anchors prevents their storage from vanishing
        // when the four-transistor physical cells are composed later.
        std::set<std::size_t> passive_ids, passive_nodes;
        for (std::size_t i = 0; i < original.size(); ++i)
            if (!claimed[i]) {
                const auto& e = original[i];
                if (e.kind != law::resistor && e.kind != law::capacitor)
                    throw std::runtime_error("unbound non-passive TX law");
                passive_ids.insert(i);
                passive_nodes.insert(sim.root(e.a));
                passive_nodes.insert(sim.root(e.b));
            }
        std::vector<named_port> passive_ports;
        for (const auto node : passive_nodes)
            passive_ports.push_back({"node_" + std::to_string(node), wires.at(node)});
        block("remaining_passives", "OneGTransmitPassives", [&](const auto& e) {
            const auto index = static_cast<std::size_t>(&e - original.data());
            return passive_ids.count(index);
        }, passive_ports);
        if (std::find(claimed.begin(), claimed.end(), false) != claimed.end())
            throw std::runtime_error("incomplete TX physical partition");
        manifest << "\n  ]\n}\n";
        if (!manifest)
            throw std::runtime_error("incomplete TX partition manifest");
        sim.elements = original;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
