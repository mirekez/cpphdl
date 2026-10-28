#include "../cascoded_core_transmit_driver.hpp"
#include <VerilogA/export.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        // Read the actual TX setting in a separate definition context, so
        // unrelated, unelaborated output stages are absent from this export.
        const auto storage = [] {
            simulation definitions;
            one_g::ihp_sg13g2::cascoded_core_emphasized_pam5_driver transmitter;
            return transmitter.shared_translation_storage;
        }();
        simulation sim;
        level_shift_bias bias;
        bias.storage = storage;
        bias.name("OneGTransmitTranslationBias");
        bias.elaborate();
        const std::vector<verilog_a::named_port> ports{
            {"VDD", bias.Vdd}, {"VSS", bias.Vss},
            {"VLOGIC", bias.Vlogic}, {"BIAS", bias.BIAS}};
        verilog_a::detail::write_file(destination / "translation_bias.va",
            verilog_a::render_simulation_model(sim, "OneGTransmitTranslationBias", ports));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
