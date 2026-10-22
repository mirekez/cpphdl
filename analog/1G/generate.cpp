#include "phy.hpp"
#include "IHPSG13G2/current_steering_driver.hpp"
#include "IHPSG13G2/biased_driver.hpp"
#include <VerilogA/export.hpp>
#include <iostream>

using namespace analog;
using namespace analog::one_g;
using analog::verilog_a::named_port;
template <class T, class Interface>
void emit(const std::filesystem::path& root, const std::filesystem::path& destination, const char* name,
          Interface interface) {
    simulation sim;
    T component;
    component.name(name);
    component.elaborate();
    for (auto& element : sim.elements)
        element.file =
            std::filesystem::relative(std::filesystem::weakly_canonical(element.file), root).generic_string();
    auto file = destination / "generated" / (std::string(name) + ".va");
    std::filesystem::create_directories(file.parent_path());
    verilog_a::detail::write_file(file, verilog_a::render_simulation_model(sim, name, interface(component)));
}
#define P(NAME)                                                                                                        \
    named_port {                                                                                                       \
        #NAME, c.NAME                                                                                                  \
    }
#define EXPORT(TYPE, NAME, ...)                                                                                        \
    emit<TYPE>(root, destination, NAME, [](auto& c) { return std::vector<named_port>{P(Vdd), P(Vss), __VA_ARGS__}; })

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        std::filesystem::path root = std::filesystem::canonical(argv[1]), destination(argv[2]);
        EXPORT(pam5_dac, "OneGPAM5DAC", P(VREF), P(VCM), P(OUT), named_port{"C0", c.CODE[0]},
               named_port{"C1", c.CODE[1]}, named_port{"C2", c.CODE[2]}, named_port{"C3", c.CODE[3]});
        EXPORT(sim_pam5_slicer, "OneGPAM5Slicer", P(IN), P(VCM), P(VLOGIC), named_port{"C0", c.CODE[0]},
               named_port{"C1", c.CODE[1]}, named_port{"C2", c.CODE[2]}, named_port{"C3", c.CODE[3]});
        EXPORT(sim_symbol_memory, "OneGSymbolMemory", P(IN), P(CLOCK), P(OUT));
        EXPORT(pulse_shaper, "OneGPulseShaper", P(MAIN), P(HISTORY), P(VCM), P(OUT));
        EXPORT(line_driver, "OneGLineDriver", P(IN), P(VCM), P(P), P(N));
        emit<ihp_sg13g2::current_steering_driver>(root, destination / "IHPSG13G2", "OneGMOSLineDriver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned i = 0; i < 4; ++i) {
                ports.push_back({"GP" + std::to_string(i), c.GATE_P[i]});
                ports.push_back({"GN" + std::to_string(i), c.GATE_N[i]});
            }
            return ports;
        });
        emit<ihp_sg13g2::biased_pam5_driver>(root, destination / "IHPSG13G2", "OneGBiasedLineDriver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned i = 0; i < 4; ++i) {
                ports.push_back({"GP" + std::to_string(i), c.GATE_P[i]});
                ports.push_back({"GN" + std::to_string(i), c.GATE_N[i]});
            }
            return ports;
        });
        emit<ihp_sg13g2::trimmed_pam5_driver>(root, destination / "IHPSG13G2", "OneGTrimmedLineDriver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned i = 0; i < 4; ++i) {
                ports.push_back({"GP" + std::to_string(i), c.GATE_P[i]});
                ports.push_back({"GN" + std::to_string(i), c.GATE_N[i]});
            }
            for (unsigned i = 0; i < 6; ++i)
                ports.push_back({"TRIM_N" + std::to_string(i), c.reference.TRIM_N[i]});
            return ports;
        });
        emit<transmitter>(root, destination, "OneGTransmitter", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(CLOCK), P(VREF), P(VCM), P(P), P(N), P(REPLICA)};
            for (unsigned i = 0; i < 4; ++i)
                ports.push_back({"C" + std::to_string(i), c.CODE[i]});
            return ports;
        });
        EXPORT(cable_pair, "OneGCablePair", P(AP), P(AN), P(BP), P(BN), P(VCM));
        emit<four_pair_cable>(root, destination, "OneGFourPairCable", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(VCM)};
            for (unsigned i = 0; i < pairs; ++i) {
                auto suffix = std::to_string(i);
                ports.insert(ports.end(), {{"AP" + suffix, c.AP[i]},
                                           {"AN" + suffix, c.AN[i]},
                                           {"BP" + suffix, c.BP[i]},
                                           {"BN" + suffix, c.BN[i]}});
            }
            return ports;
        });
        EXPORT(hybrid, "OneGHybrid", P(P), P(N), P(LOCAL), P(VCM), P(OUT));
        EXPORT(ctle, "OneGCTLE", P(IN), P(VCM), P(OUT));
        EXPORT(dfe, "OneGDFE", P(IN), P(CLOCK), P(VCM), P(VREF), P(VLOGIC), P(OUT));
        EXPORT(receiver, "OneGReceiver", P(P), P(N), P(LOCAL), P(VCM), P(OUT));
        emit<pair_link>(root, destination, "OneGPairLink", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(CLOCK), P(VREF), P(VCM), P(OUTA), P(OUTB)};
            for (unsigned i = 0; i < 4; ++i) {
                ports.push_back({"A" + std::to_string(i), c.A[i]});
                ports.push_back({"B" + std::to_string(i), c.B[i]});
            }
            return ports;
        });
        emit<link>(root, destination, "OneGLink", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(CLOCK), P(VREF), P(VCM)};
            for (unsigned lane = 0; lane < pairs; ++lane) {
                auto suffix = std::to_string(lane);
                ports.push_back({"OUTA" + suffix, c.OUTA[lane]});
                ports.push_back({"OUTB" + suffix, c.OUTB[lane]});
                for (unsigned bit = 0; bit < 4; ++bit) {
                    ports.push_back({"A" + suffix + "C" + std::to_string(bit), c.A[lane][bit]});
                    ports.push_back({"B" + suffix + "C" + std::to_string(bit), c.B[lane][bit]});
                }
            }
            return ports;
        });
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#undef EXPORT
#undef P
