#include "../lead_receive_common_reference.hpp"
#include <VerilogA/export.hpp>
#include <fstream>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::ihp_sg13g2::lead_receive_common_reference reference;
        reference.elaborate();
        using verilog_a::named_port;
        const std::vector<named_port> ports{{"VDD", reference.Vdd}, {"VSS", reference.Vss},
                                          {"OUT", reference.OUT}};
        const auto node_name = [&](const electrical& terminal) {
            const auto node = sim.root(terminal.id());
            for (const auto& port : ports)
                if (sim.root(port.terminal.id()) == node)
                    return port.name;
            return std::string("cpphdl_n") + std::to_string(node);
        };
        verilog_a::detail::write_file(destination / "reference.va",
            verilog_a::render_simulation_model(sim, "ReceiveCommonReference", ports));
        const auto& amplifier = reference.buffer.amplifier;
        const std::vector<named_port> probes{{"TARGET", reference.target.TAP},
            {"DRIVE", reference.buffer.DRIVE}, {"BIAS_N", reference.n_bias.TAP},
            {"BIAS_P", reference.p_bias.TAP}, {"TAIL_BIAS", reference.TAIL_BIAS},
            {"LEFT", amplifier.LEFT}, {"RIGHT", amplifier.RIGHT}, {"TAIL", amplifier.TAIL},
            {"MIRROR", amplifier.MIRROR}, {"PMIRROR", amplifier.PMIRROR}, {"POUTPUT", amplifier.POUTPUT}};
        std::ofstream output(destination / "probes.json");
        output << "{\n";
        for (std::size_t index = 0; index < probes.size(); ++index) {
            if (index)
                output << ",\n";
            output << "  \"" << probes[index].name << "\": \"" << node_name(probes[index].terminal) << "\"";
        }
        output << "\n}\n";
        if (!output)
            throw std::runtime_error("incomplete receive reference probe export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
