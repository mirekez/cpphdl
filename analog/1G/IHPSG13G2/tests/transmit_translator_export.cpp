#include "../../clock_level_shifter.hpp"
#include <VerilogA/export.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::clock_level_shifter translator;
        translator.name("OneGTransmitTranslator");
        translator.elaborate();
        const std::vector<verilog_a::named_port> ports{
            {"VDD", translator.Vdd}, {"VSS", translator.Vss},
            {"VLOGIC", translator.Vlogic}, {"BIAS", translator.BIAS},
            {"A", translator.A}, {"Y", translator.Y}, {"Y_N", translator.Y_N}};
        verilog_a::detail::write_file(destination / "translator.va",
            verilog_a::render_simulation_model(sim, "OneGTransmitTranslator", ports));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
