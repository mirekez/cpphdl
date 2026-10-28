#include "phy.hpp"
#include "IHPSG13G2/current_steering_driver.hpp"
#include "IHPSG13G2/reference_clock_generator.hpp"
#include "IHPSG13G2/processing_clock_oscillator.hpp"
#include "IHPSG13G2/processing_clock_gate.hpp"
#include "IHPSG13G2/processing_clock_delivery.hpp"
#include "IHPSG13G2/biased_driver.hpp"
#include "IHPSG13G2/emphasized_driver.hpp"
#include "IHPSG13G2/core_transmit_driver.hpp"
#include "IHPSG13G2/programmable_core_transmit_driver.hpp"
#include "IHPSG13G2/cascoded_core_transmit_driver.hpp"
#include "IHPSG13G2/charge_balanced_receive_frontend.hpp"
#include "IHPSG13G2/precision_receive_frontend.hpp"
#include "IHPSG13G2/mdi_receive_frontend.hpp"
#include "IHPSG13G2/wideband_receive_frontend.hpp"
#include "IHPSG13G2/precise_wideband_receive_frontend.hpp"
#include "IHPSG13G2/parallel_receive_frontend.hpp"
#include "IHPSG13G2/balanced_receive_frontend.hpp"
#include "IHPSG13G2/programmable_receive_gain.hpp"
#include "IHPSG13G2/differential_receive_gain.hpp"
#include "IHPSG13G2/receive_reference.hpp"
#include "IHPSG13G2/receive_common_reference.hpp"
#include "IHPSG13G2/lead_receive_common_reference.hpp"
#include "IHPSG13G2/core_receive_dac.hpp"
#include "IHPSG13G2/core_receive_decision.hpp"
#include "IHPSG13G2/differential_receive_frontend.hpp"
#include "IHPSG13G2/programmable_receive_frontend.hpp"
#include "IHPSG13G2/wideband_programmable_receive_frontend.hpp"
#include "passive_hybrid.hpp"
#include "mdi_input_network.hpp"
#include "biased_mdi_input_network.hpp"
#include "adc.hpp"
#include "adc_feedback.hpp"
#include "adc_core.hpp"
#include "receive_frontend.hpp"
#include "receive_clock_oscillator.hpp"
#include "flp_frontend.hpp"
#include "reference_clock.hpp"
#include "clock_level_shifter.hpp"
#include "reference_clock_domain.hpp"
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


// Every receive variant shares member ports; elaboration selects its circuit.
template <class Receiver, bool Programmable = false>
void emit_receiver(const std::filesystem::path& root, const std::filesystem::path& destination, const char* name) {
    emit<Receiver>(root, destination, name, [](auto& c) {
        std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(PLUS), P(MINUS), P(EVALUATE), P(CLEAR)};
        if constexpr (Programmable)
            for (std::size_t bit = 0; bit < c.SELECT_N.size(); ++bit)
                ports.push_back({"SELECT_N" + std::to_string(bit), c.SELECT_N[bit]});
        for (std::size_t i = 0; i < c.TRACK.size(); ++i) {
            const auto suffix = std::to_string(i);
            ports.push_back({"TRACK" + suffix, c.TRACK[i]});
            ports.push_back({"CORE_DECISION" + suffix, c.CORE_DECISION[i]});
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + suffix + "_" + std::to_string(bit), c.BIT_N[i][bit]});
        }
        return ports;
    });
}

// Differential receiver variants expose their shared physical reference.
template <class Receiver, bool Mdi = false>
void emit_referenced_receiver(const std::filesystem::path& root, const std::filesystem::path& destination,
                              const char* name) {
    emit<Receiver>(root, destination, name, [](auto& c) {
        std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(VREF), P(EVALUATE), P(CLEAR)};
        if constexpr (Mdi) {
            ports.push_back(P(P));
            ports.push_back(P(N));
        } else {
            ports.push_back(P(PLUS));
            ports.push_back(P(MINUS));
        }
        for (unsigned bit = 0; bit < c.SELECT_N.size(); ++bit)
            ports.push_back({"SELECT_N" + std::to_string(bit), c.SELECT_N[bit]});
        for (unsigned slice = 0; slice < c.TRACK.size(); ++slice) {
            const auto suffix = std::to_string(slice);
            ports.push_back({"TRACK" + suffix, c.TRACK[slice]});
            ports.push_back({"CORE_DECISION" + suffix, c.CORE_DECISION[slice]});
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + suffix + "_" + std::to_string(bit), c.BIT_N[slice][bit]});
        }
        return ports;
    });
}

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
        EXPORT(passive_hybrid, "OneGPassiveHybrid", P(P), P(N), P(LOCAL_P), P(LOCAL_N), P(VCM), P(OUTP), P(OUTN));
        EXPORT(mdi_input_network, "OneGMDIInputNetwork", P(P), P(N), P(PLUS), P(MINUS));
        EXPORT(biased_mdi_input_network, "OneGBiasedMDIInputNetwork", P(P), P(N), P(VCM), P(PLUS), P(MINUS));
        EXPORT(flp_pulse_driver, "OneGFLPPulseDriver", P(COMMAND), P(ENABLE), P(P), P(N));
        EXPORT(flp_pulse_detector, "OneGFLPPulseDetector", P(P), P(N), P(PULSE));
        EXPORT(reference_clock_multiplier, "OneGReferenceClockMultiplier", P(REF), P(CLEAR), P(OUT), P(DIVIDED));
        EXPORT(reference_clock_domain, "OneGReferenceClockDomain", P(Vlogic), P(REF), P(CLEAR), P(OUT), P(OUT_N));
        emit<ihp_sg13g2::reference_clock_generator>(root, destination / "IHPSG13G2", "OneGIHPReferenceClockGenerator", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(Vlogic), P(REF), P(CLEAR), P(OUT), P(OUT_N)};
        });
        emit<ihp_sg13g2::processing_clock_oscillator>(root, destination / "IHPSG13G2", "OneGIHPProcessingClockOscillator", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(CLEAR), P(OUT)};
            for (unsigned bit = 0; bit < 8; ++bit) {
                ports.push_back({"COARSE_N" + std::to_string(bit), c.COARSE_N[bit]});
                ports.push_back({"FINE_N" + std::to_string(bit), c.FINE_N[bit]});
            }
            return ports;
        });
        emit<ihp_sg13g2::processing_clock_gate>(root, destination / "IHPSG13G2", "OneGIHPProcessingClockGate", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(CLOCK), P(ENABLE), P(OUT)};
        });
        emit<ihp_sg13g2::processing_clock_delivery>(root, destination / "IHPSG13G2", "OneGIHPProcessingClockDelivery", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(CLEAR), P(ADMIT), P(RAW), P(OUT)};
            for (unsigned bit = 0; bit < 8; ++bit) {
                ports.push_back({"COARSE_N" + std::to_string(bit), c.COARSE_N[bit]});
                ports.push_back({"FINE_N" + std::to_string(bit), c.FINE_N[bit]});
            }
            return ports;
        });
        EXPORT(clock_level_shifter, "OneGClockLevelShifter", P(Vlogic), P(BIAS), P(A), P(Y), P(Y_N));
        emit<ihp_sg13g2::core_receive_dac>(root, destination / "IHPSG13G2", "OneGIHPCoreReceiveDAC", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(OUT)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"TRIAL" + std::to_string(bit), c.TRIAL[bit]});
            return ports;
        });
        emit<ihp_sg13g2::core_receive_decision>(root, destination / "IHPSG13G2", "OneGIHPCoreReceiveDecision", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(IN), P(EVALUATE), P(CLEAR), P(CORE_DECISION)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"TRIAL" + std::to_string(bit), c.TRIAL[bit]});
            return ports;
        });
        emit<ihp_sg13g2::emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.GATE_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.GATE_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            return ports;
        });
        emit<ihp_sg13g2::core_emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGCoreEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.MAIN_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.MAIN_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            return ports;
        });
        emit<ihp_sg13g2::programmable_emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGProgrammableEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.GATE_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.GATE_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            for (unsigned bit = 0; bit < 6; ++bit)
                ports.push_back({"TRIM_N" + std::to_string(bit), c.TRIM_N[bit]});
            return ports;
        });
        emit<ihp_sg13g2::programmable_core_emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGProgrammableCoreEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.MAIN_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.MAIN_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            for (unsigned bit = 0; bit < 6; ++bit)
                ports.push_back({"TRIM_N" + std::to_string(bit), c.TRIM_N[bit]});
            return ports;
        });
        emit<ihp_sg13g2::cascoded_emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGCascodedEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.GATE_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.GATE_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            for (unsigned bit = 0; bit < 6; ++bit)
                ports.push_back({"TRIM_N" + std::to_string(bit), c.TRIM_N[bit]});
            return ports;
        });
        emit<ihp_sg13g2::cascoded_core_emphasized_pam5_driver>(root, destination / "IHPSG13G2", "OneGCascodedCoreEmphasizedPAM5Driver", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(P), P(N)};
            for (unsigned bit = 0; bit < 4; ++bit) {
                ports.push_back({"MAIN_P" + std::to_string(bit), c.MAIN_P[bit]});
                ports.push_back({"MAIN_N" + std::to_string(bit), c.MAIN_N[bit]});
                ports.push_back({"HISTORY_P" + std::to_string(bit), c.HISTORY_P[bit]});
                ports.push_back({"HISTORY_N" + std::to_string(bit), c.HISTORY_N[bit]});
            }
            for (unsigned bit = 0; bit < 6; ++bit)
                ports.push_back({"TRIM_N" + std::to_string(bit), c.TRIM_N[bit]});
            return ports;
        });
        emit<SARADCFrontend>(root, destination, "OneGSARADCFrontend", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN), P(TRACK), P(EVALUATE), P(POSITIVE), P(NEGATIVE)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + std::to_string(bit), c.BIT_N[bit]});
            return ports;
        });
        emit<SARADCFeedbackFrontend>(root, destination, "OneGSARADCFeedbackFrontend", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN), P(TRACK), P(EVALUATE), P(POSITIVE),
                                          P(NEGATIVE), P(CLEAR), P(DECISION), P(DECISION_N)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + std::to_string(bit), c.BIT_N[bit]});
            return ports;
        });
        emit<SARADCArray<16>>(root, destination, "OneGSARADCArray", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN), P(EVALUATE), P(CLEAR)};
            for (unsigned i = 0; i < 16; ++i) {
                auto suffix = std::to_string(i);
                ports.push_back({"TRACK" + suffix, c.TRACK[i]});
                ports.push_back({"DECISION" + suffix, c.DECISION[i]});
                ports.push_back({"DECISION_N" + suffix, c.DECISION_N[i]});
                for (unsigned bit = 0; bit < 8; ++bit)
                    ports.push_back({"BIT_N" + suffix + "_" + std::to_string(bit), c.BIT_N[i][bit]});
            }
            return ports;
        });
        emit<HighRateSARADCFeedbackFrontend>(root, destination, "OneGHighRateSARADCFeedbackFrontend", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN), P(TRACK), P(EVALUATE), P(POSITIVE),
                                          P(NEGATIVE), P(CLEAR), P(DECISION), P(DECISION_N)};
            for (unsigned bit = 0; bit < 8; ++bit)
                ports.push_back({"BIT_N" + std::to_string(bit), c.BIT_N[bit]});
            return ports;
        });
        emit<SARADCCoreArray<16>>(root, destination, "OneGSARADCCoreArray", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(Vlogic), P(IN), P(EVALUATE), P(CLEAR)};
            for (unsigned i = 0; i < 16; ++i) {
                auto suffix = std::to_string(i);
                ports.push_back({"TRACK" + suffix, c.TRACK[i]});
                ports.push_back({"DECISION" + suffix, c.DECISION[i]});
                ports.push_back({"DECISION_N" + suffix, c.DECISION_N[i]});
                ports.push_back({"CORE_DECISION" + suffix, c.CORE_DECISION[i]});
                for (unsigned bit = 0; bit < 8; ++bit)
                    ports.push_back({"BIT_N" + suffix + "_" + std::to_string(bit), c.BIT_N[i][bit]});
            }
            return ports;
        });
        emit_receiver<PAM5ReceiveFrontend<16>>(root, destination, "OneGPAM5ReceiveFrontend");
        emit_receiver<ihp_sg13g2::receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPReceiveFrontend");
        emit_receiver<ihp_sg13g2::wideband_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPWidebandReceiveFrontend");
        emit_receiver<ihp_sg13g2::precise_wideband_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPPreciseWidebandReceiveFrontend");
        emit_receiver<ihp_sg13g2::parallel_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPParallelReceiveFrontend");
        emit_receiver<ihp_sg13g2::balanced_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPBalancedReceiveFrontend");
        emit_receiver<ihp_sg13g2::programmable_receive_frontend<16>, true>(root, destination / "IHPSG13G2", "OneGIHPProgrammableReceiveFrontend");
        emit_receiver<ihp_sg13g2::wideband_programmable_receive_frontend<16>, true>(root, destination / "IHPSG13G2", "OneGIHPWidebandProgrammableReceiveFrontend");
        emit_receiver<ihp_sg13g2::charge_balanced_receive_frontend<16>, true>(root, destination / "IHPSG13G2", "OneGIHPChargeBalancedReceiveFrontend");
        emit<ihp_sg13g2::programmable_receive_gain>(root, destination / "IHPSG13G2", "OneGIHPProgrammableReceiveGain", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN), P(OUT)};
            for (unsigned i = 0; i < c.SELECT_N.size(); ++i)
                ports.push_back({"SELECT_N" + std::to_string(i), c.SELECT_N[i]});
            return ports;
        });
        emit<ihp_sg13g2::differential_receive_preamplifier>(root, destination / "IHPSG13G2", "OneGIHPDifferentialReceivePreamplifier", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(PLUS), P(MINUS), P(VREF), P(OUT)};
            for (unsigned bit = 0; bit < c.SELECT_N.size(); ++bit)
                ports.push_back({"SELECT_N" + std::to_string(bit), c.SELECT_N[bit]});
            return ports;
        });
        emit_referenced_receiver<ihp_sg13g2::differential_receive_frontend<13>>(root,
            destination / "IHPSG13G2", "OneGIHPCompactDifferentialReceiveFrontend");
        emit_referenced_receiver<ihp_sg13g2::differential_mdi_receiver<13>, true>(root,
            destination / "IHPSG13G2", "OneGIHPCompactDifferentialMDIReceiver");
        emit_referenced_receiver<ihp_sg13g2::differential_receive_frontend<16>>(root,
            destination / "IHPSG13G2", "OneGIHPDifferentialReceiveFrontend");
        emit_referenced_receiver<ihp_sg13g2::differential_mdi_receiver<16>, true>(root,
            destination / "IHPSG13G2", "OneGIHPDifferentialMDIReceiver");
        emit<ihp_sg13g2::adc_range_reference>(root, destination / "IHPSG13G2", "OneGIHPADCRangeReference", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(OUT)};
        });
        emit<ihp_sg13g2::receive_reference>(root, destination / "IHPSG13G2", "OneGIHPReceiveReference", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(OUT)};
        });
        emit<ihp_sg13g2::receive_common_reference>(root, destination / "IHPSG13G2", "OneGIHPReceiveCommonReference", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(OUT)};
        });
        emit<ihp_sg13g2::lead_receive_common_reference>(root, destination / "IHPSG13G2", "OneGIHPLeadReceiveCommonReference", [](auto& c) {
            return std::vector<named_port>{P(Vdd), P(Vss), P(OUT)};
        });
        emit<ihp_sg13g2::differential_receive_gain>(root, destination / "IHPSG13G2", "OneGIHPDifferentialReceiveGain", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(IN_PLUS), P(IN_MINUS), P(VREF), P(OUT)};
            for (unsigned i = 0; i < c.SELECT_N.size(); ++i)
                ports.push_back({"SELECT_N" + std::to_string(i), c.SELECT_N[i]});
            return ports;
        });
        emit_receiver<ihp_sg13g2::mdi_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPMDIReceiveFrontend");
        emit_receiver<ihp_sg13g2::precision_receive_frontend<16>>(root, destination / "IHPSG13G2", "OneGIHPPrecisionReceiveFrontend");
        emit<receive_clock_oscillator>(root, destination, "OneGReceiveClockOscillator", [](auto& c) {
            std::vector<named_port> ports{P(Vdd), P(Vss), P(RESET), P(OUT)};
            for (unsigned bit = 0; bit < 8; ++bit) {
                ports.push_back({"COARSE_N" + std::to_string(bit), c.COARSE_N[bit]});
                ports.push_back({"FINE_N" + std::to_string(bit), c.FINE_N[bit]});
            }
            return ports;
        });
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
