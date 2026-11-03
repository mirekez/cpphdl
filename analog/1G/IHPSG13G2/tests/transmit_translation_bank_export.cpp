#include "../cascoded_core_transmit_driver.hpp"
#include <VerilogA/export.hpp>
#include <fstream>
#include <iostream>
#include <set>

using namespace analog;

// Export the actual translator instances after elaborating the complete TX.
// The electrical bank test supplies loads at these explicit IC boundaries;
// this export does not claim to realize the remaining output-current stage.
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::ihp_sg13g2::cascoded_core_emphasized_pam5_driver transmitter;
        transmitter.elaborate();
        const auto original = sim.elements;
        using verilog_a::named_port;
        std::ofstream manifest(destination / "bank.json");
        manifest << "{\n  \"storage_F\": " << std::setprecision(17)
                 << transmitter.shared_translation_storage.value << ",\n  \"translators\": [\n";
        std::set<std::size_t> inputs, outputs;
        const auto replica_node = sim.root(transmitter.translation_bias.BIAS.id());
        const auto check_supplies = [&](const auto& component) {
            if (sim.root(component.Vdd.id()) != sim.root(transmitter.Vdd.id()) ||
                    sim.root(component.Vss.id()) != sim.root(transmitter.Vss.id()) ||
                    sim.root(component.Vlogic.id()) != sim.root(transmitter.Vlogic.id()))
                throw std::runtime_error("bank IC supply differs from the parent TX");
        };
        check_supplies(transmitter.translation_bias);
        const auto export_block = [&](const std::string& filename, const std::string& model,
                                      const std::set<std::size_t>& owners,
                                      const std::vector<named_port>& ports) {
            std::set<std::size_t> boundary, inside, outside;
            for (const auto& port : ports)
                boundary.insert(sim.root(port.terminal.id()));
            if (boundary.size() != ports.size())
                throw std::runtime_error("aliased bank IC boundary");
            sim.elements.clear();
            for (const auto& element : original) {
                const bool selected = owners.count(element.component);
                for (const auto node : verilog_a::detail::terminals(element))
                    (selected ? inside : outside).insert(sim.root(node));
                if (selected)
                    sim.elements.push_back(element);
            }
            for (const auto node : inside)
                if (outside.count(node) && !boundary.count(node))
                    throw std::runtime_error("undeclared bank IC connection");
            for (const auto node : boundary)
                if (!inside.count(node))
                    throw std::runtime_error("unconnected bank IC port");
            verilog_a::detail::write_file(destination / filename,
                verilog_a::render_simulation_model(sim, model, ports));
            return sim.elements.size();
        };
        for (std::size_t index = 0; index < transmitter.gates.size() + transmitter.trim_gates.size(); ++index) {
            const auto& gate = index < transmitter.gates.size()
                ? transmitter.gates[index] : transmitter.trim_gates[index - transmitter.gates.size()];
            check_supplies(gate);
            const auto bit = index % 4;
            const auto input = index < 4 ? transmitter.MAIN_P[bit].id()
                : index < 8 ? transmitter.MAIN_N[bit].id()
                : index < 12 ? transmitter.HISTORY_P[bit].id()
                : index < 16 ? transmitter.HISTORY_N[bit].id() : transmitter.TRIM_N[index - 16].id();
            const auto output = index < 4 ? transmitter.output.GATE_P[bit].id()
                : index < 8 ? transmitter.output.GATE_N[bit].id()
                : index < 12 ? transmitter.output.HISTORY_P[bit].id()
                : index < 16 ? transmitter.output.HISTORY_N[bit].id() : transmitter.output.TRIM_N[index - 16].id();
            if (sim.root(input) != sim.root(gate.A.id()) || sim.root(output) != sim.root(gate.Y.id()))
                throw std::runtime_error("bank translator is disconnected from its actual TX endpoints");
            if (sim.root(gate.BIAS.id()) != replica_node ||
                    !inputs.insert(sim.root(gate.A.id())).second ||
                    !outputs.insert(sim.root(gate.Y.id())).second)
                throw std::runtime_error("translator bank sharing or independence changed");
            const std::set<std::size_t> owners{gate.id(), gate.input_inverter.id(),
                gate.output_inverter[0].id(), gate.output_inverter[1].id()};
            const auto filename = "translator_" + std::to_string(index) + ".va";
            const auto count = export_block(filename, "OneGTransmitTranslator", owners,
                {{"VDD", gate.Vdd}, {"VSS", gate.Vss}, {"VLOGIC", gate.Vlogic},
                 {"BIAS", gate.BIAS}, {"A", gate.A}, {"Y", gate.Y}, {"Y_N", gate.Y_N}});
            if (count != 36)
                throw std::runtime_error("translator device inventory changed");
            if (index)
                manifest << ",\n";
            manifest << "    {\"file\": \"" << filename << "\", \"elements\": " << count
                     << ", \"input_node\": " << sim.root(gate.A.id())
                     << ", \"output_node\": " << sim.root(gate.Y.id())
                     << ", \"bias_node\": " << replica_node << "}";
        }
        for (const auto node : inputs)
            if (outputs.count(node))
                throw std::runtime_error("unexpected feedback between bank outputs and commands");
        const auto& bias = transmitter.translation_bias;
        const auto count = export_block("translation_bias.va", "OneGTransmitTranslationBias", {bias.id()},
            {{"VDD", bias.Vdd}, {"VSS", bias.Vss}, {"VLOGIC", bias.Vlogic}, {"BIAS", bias.BIAS}});
        if (count != 5)
            throw std::runtime_error("shared replica inventory changed");
        manifest << "\n  ],\n  \"replica_elements\": " << count << ",\n  \"bias_node\": "
                 << replica_node << "\n}\n";
        if (!manifest)
            throw std::runtime_error("incomplete bank manifest");
        sim.elements = original;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
