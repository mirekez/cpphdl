#include "../differential_receive_frontend.hpp"
#include <VerilogA/export.hpp>
#include <fstream>
#include <iostream>
#include <set>

using namespace analog;

// Export the sampler after the actual cold receive slice configures it.
// Its parent preamplifier and decision circuit remain external loads.
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::ihp_sg13g2::cold_charge_balanced_receive_slice slice;
        slice.elaborate();
        auto& sampler = slice.sampler;
        const std::set<std::size_t> owners{sampler.id(), sampler.complement.id()};
        const std::vector<verilog_a::named_port> ports{
            {"VDD", sampler.Vdd}, {"VSS", sampler.Vss}, {"IN", sampler.IN},
            {"CLOCK", sampler.CLOCK}, {"OUT", sampler.OUT}};
        std::set<std::size_t> boundary, inside, outside;
        for (const auto& port : ports)
            boundary.insert(sim.root(port.terminal.id()));
        if (boundary.size() != ports.size())
            throw std::runtime_error("aliased sampler boundary");
        const auto original = sim.elements;
        sim.elements.clear();
        std::vector<std::size_t> identities;
        for (std::size_t index = 0; index < original.size(); ++index) {
            const auto& element = original[index];
            const bool selected = owners.count(element.component);
            for (const auto node : verilog_a::detail::terminals(element))
                (selected ? inside : outside).insert(sim.root(node));
            if (selected) {
                identities.push_back(index);
                sim.elements.push_back(element);
            }
        }
        for (const auto node : inside)
            if (outside.count(node) && !boundary.count(node))
                throw std::runtime_error("undeclared sampler connection to its parent");
        for (const auto node : boundary)
            if (!inside.count(node))
                throw std::runtime_error("unconnected sampler port");
        if (identities.size() != 11)
            throw std::runtime_error("changed sampler contribution inventory");
        verilog_a::detail::write_file(destination / "sampler.va",
            verilog_a::render_simulation_model(sim, "ReceiveSampler", ports));
        std::ofstream manifest(destination / "sampler.json");
        manifest << "{\n  \"parent_contributions\": " << original.size()
                 << ",\n  \"selected_contributions\": [";
        for (std::size_t index = 0; index < identities.size(); ++index)
            manifest << (index ? ", " : "") << identities[index];
        manifest << "],\n  \"clock_inverse\": \"cpphdl_n"
                 << sim.root(sampler.complement.Y.id()) << "\"\n}\n";
        if (!manifest)
            throw std::runtime_error("incomplete sampler export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
