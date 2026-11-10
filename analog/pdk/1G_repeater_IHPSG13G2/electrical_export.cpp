#include "repeater_analog.hpp"
#include <VerilogA/export.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <cxxabi.h>

using namespace analog;

// Export the actual elaborated terminals, not an independently drawn block
// diagram. Digital names refer to scalar bits of RepeaterDigital's ports.
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        auto storage = std::make_unique<one_g::ihp_sg13g2::repeater_analog>();
        auto& chip = *storage;
        chip.elaborate();
        std::vector<verilog_a::named_port> ports;
        struct Binding { std::string name, direction, digital; std::size_t node; };
        std::vector<Binding> bindings;
        std::map<std::string, std::size_t> anchors;
        const auto anchor = [&](const std::string& name, electrical wire) {
            if (!anchors.emplace(name, sim.root(wire.id())).second)
                throw std::runtime_error("duplicate terminal: " + name);
        };
        const auto port = [&](const std::string& name, electrical wire,
                              const std::string& direction, const std::string& digital = "") {
            ports.push_back({name, wire});
            bindings.push_back({name, direction, digital, sim.root(wire.id())});
            anchor(name, wire);
        };
        const auto index = [](const std::string& name, unsigned i) {
            return name + "[" + std::to_string(i) + "]";
        };
        port("VDD", chip.Vdd, "inout");
        port("VSS", chip.Vss, "inout");
        port("VLOGIC", chip.Vlogic, "inout");
        port("REF_CLOCK", chip.REF_CLOCK, "input", "ref_clk");
        port("CLOCK_CLEAR", chip.CLOCK_CLEAR, "input", "reset");
        port("ADC_CLOCK", chip.ADC_CLOCK, "output", "adc_clk");
        port("PROCESSING_RAW", chip.PROCESSING_RAW, "output", "raw_clk");
        port("PROCESSING_CLOCK", chip.PROCESSING_CLOCK, "output", "pcs_clk");
        port("PROCESSING_CLEAR", chip.PROCESSING_CLEAR, "input", "processing_clear_out");
        port("PROCESSING_ADMIT", chip.PROCESSING_ADMIT, "input", "processing_admit_out");
        anchor("adc_clock.CORE_OUT", chip.adc_clock.CORE_OUT);
        anchor("adc_clock.REF", chip.adc_clock.REF);
        anchor("adc_clock.CLEAR", chip.adc_clock.CLEAR);
        anchor("adc_clock.Vlogic", chip.adc_clock.Vlogic);
        anchor("processing.RAW", chip.processing_clock.RAW);
        anchor("processing.OUT", chip.processing_clock.OUT);
        anchor("processing.CLEAR", chip.processing_clock.CLEAR);
        anchor("processing.ADMIT", chip.processing_clock.ADMIT);
        anchor("processing.Vdd", chip.processing_clock.Vdd);
        anchor("processing.Vss", chip.processing_clock.Vss);
        for (unsigned bit = 0; bit < 8; ++bit) {
            port("PROCESSING_COARSE_N_" + std::to_string(bit), chip.PROCESSING_COARSE_N[bit],
                 "input", index("processing_coarse_n_out", bit));
            port("PROCESSING_FINE_N_" + std::to_string(bit), chip.PROCESSING_FINE_N[bit],
                 "input", index("processing_fine_n_out", bit));
            anchor(index("processing.COARSE_N", bit), chip.processing_clock.COARSE_N[bit]);
            anchor(index("processing.FINE_N", bit), chip.processing_clock.FINE_N[bit]);
        }
        for (unsigned p = 0; p < 2; ++p) {
            const auto suffix = std::to_string(p);
            port("PULSE_COMMAND_" + suffix, chip.PULSE_COMMAND[p], "input", index("pulse_out", p));
            port("PULSE_ENABLE_" + suffix, chip.PULSE_ENABLE[p], "input", index("pulse_enable_out", p));
            port("PULSE_DETECTED_" + suffix, chip.PULSE_DETECTED[p], "output", index("pulse_in", p));
            const auto& flp = chip.negotiation[p];
            for (const auto& item : std::vector<verilog_a::named_port>{
                    {"TX_P", flp.TX_P}, {"TX_N", flp.TX_N}, {"RX_P", flp.RX_P}, {"RX_N", flp.RX_N},
                    {"COMMAND", flp.COMMAND}, {"ENABLE", flp.ENABLE}, {"PULSE", flp.PULSE},
                    {"Vdd", flp.Vdd}, {"Vss", flp.Vss}, {"Vlogic", flp.Vlogic}})
                anchor("flp" + suffix + "." + item.name, item.terminal);
            anchor("common" + suffix + ".OUT", chip.common_reference[p].OUT);
            anchor("range" + suffix + ".OUT", chip.range_reference[p].OUT);
            anchor("common" + suffix + ".Vdd", chip.common_reference[p].Vdd);
            anchor("common" + suffix + ".Vss", chip.common_reference[p].Vss);
            anchor("range" + suffix + ".Vdd", chip.range_reference[p].Vdd);
            anchor("range" + suffix + ".Vss", chip.range_reference[p].Vss);
        }
        for (unsigned lane = 0; lane < 8; ++lane) {
            const auto suffix = std::to_string(lane);
            const auto r = "rx" + suffix, t = "tx" + suffix;
            const auto& rx = chip.receivers[lane];
            const auto& tx = chip.transmitters[lane];
            port("P_" + suffix, chip.P[lane], "inout");
            port("N_" + suffix, chip.N[lane], "inout");
            port("SENSE_P_" + suffix, chip.SENSE_P[lane], "input");
            port("SENSE_N_" + suffix, chip.SENSE_N[lane], "input");
            port("CLEAR_" + suffix, chip.CLEAR[lane], "input", index("clear_out", lane));
            port("EVALUATE_ENABLE_" + suffix, chip.EVALUATE_ENABLE[lane], "input", index("evaluate_enable_out", lane));
            for (const auto& item : std::vector<verilog_a::named_port>{
                    {"P", rx.P}, {"N", rx.N}, {"Vdd", rx.Vdd}, {"Vss", rx.Vss}, {"Vlogic", rx.Vlogic},
                    {"VCM", rx.VCM}, {"VREF", rx.VREF}, {"CLEAR", rx.CLEAR}, {"ADC_CLOCK", rx.ADC_CLOCK},
                    {"EVALUATE_ENABLE", rx.EVALUATE_ENABLE}, {"input.P", rx.input.P}, {"input.N", rx.input.N},
                    {"input.VCM", rx.input.VCM}, {"input.PLUS", rx.input.PLUS}, {"input.MINUS", rx.input.MINUS},
                    {"preamp.PLUS", rx.preamplifier.PLUS}, {"preamp.MINUS", rx.preamplifier.MINUS},
                    {"preamp.VREF", rx.preamplifier.VREF}, {"preamp.OUT", rx.preamplifier.OUT},
                    {"evaluation.CLOCK", rx.evaluation.CLOCK}, {"evaluation.ENABLE", rx.evaluation.ENABLE},
                    {"evaluation.OUT", rx.evaluation.OUT}, {"evaluation.Vdd", rx.evaluation.Vdd}})
                anchor(r + "." + item.name, item.terminal);
            for (const auto& item : std::vector<verilog_a::named_port>{
                    {"P", tx.P}, {"N", tx.N}, {"Vdd", tx.Vdd}, {"Vss", tx.Vss}, {"Vlogic", tx.Vlogic},
                    {"output.P", tx.output.P}, {"output.N", tx.output.N}})
                anchor(t + "." + item.name, item.terminal);
            for (unsigned bit = 0; bit < 5; ++bit) {
                port("GAIN_N_" + suffix + "_" + std::to_string(bit), chip.GAIN_N[lane][bit], "input");
                anchor(index(r + ".gain.A", bit), rx.gain_controls[bit].A);
                anchor(index(r + ".gain.Y", bit), rx.gain_controls[bit].Y);
                anchor(index(r + ".preamp.SELECT_N", bit), rx.preamplifier.SELECT_N[bit]);
            }
            for (unsigned bit = 0; bit < 6; ++bit) {
                port("TRIM_N_" + suffix + "_" + std::to_string(bit), chip.TRIM_N[lane][bit], "input");
                anchor(index(t + ".trim.A", bit), tx.trim_gates[bit].A);
            }
            for (unsigned bank = 0; bank < 4; ++bank) {
                const std::string name = bank == 0 ? "MAIN_P" : bank == 1 ? "MAIN_N" : bank == 2 ? "HISTORY_P" : "HISTORY_N";
                const std::string digital = bank == 0 ? "main_p_out" : bank == 1 ? "main_n_out" : bank == 2 ? "history_p_out" : "history_n_out";
                for (unsigned bit = 0; bit < 4; ++bit) {
                    const auto wire = bank == 0 ? chip.MAIN_P[lane][bit] : bank == 1 ? chip.MAIN_N[lane][bit] :
                                      bank == 2 ? chip.HISTORY_P[lane][bit] : chip.HISTORY_N[lane][bit];
                    port(name + "_" + suffix + "_" + std::to_string(bit), wire, "input", index(index(digital, lane), bit));
                    anchor(index(t + ".gate.A", 4 * bank + bit), tx.gates[4 * bank + bit].A);
                }
            }
            for (unsigned slice = 0; slice < 13; ++slice) {
                const auto number = suffix + "_" + std::to_string(slice);
                const auto s = r + ".s" + std::to_string(slice);
                const auto& cell = rx.slices[slice];
                port("TRACK_" + number, chip.TRACK[lane][slice], "input", index("acquire_out", 16 * lane + slice));
                port("CONVERT_" + number, chip.CONVERT[lane][slice], "input", index("convert_out", 16 * lane + slice));
                port("DECISION_" + number, chip.DECISION[lane][slice], "output", index(index("comparisons_in", lane), slice));
                for (const auto& item : std::vector<verilog_a::named_port>{
                        {"IN", cell.IN}, {"Vdd", cell.Vdd}, {"Vss", cell.Vss}, {"Vlogic", cell.Vlogic},
                        {"CLEAR", cell.CLEAR}, {"EVALUATE", cell.EVALUATE}, {"CONVERT", cell.CONVERT},
                        {"gate.EVALUATE", cell.evaluation_gate.EVALUATE},
                        {"gate.CONVERT", cell.evaluation_gate.CONVERT}, {"gate.OUT", cell.evaluation_gate.OUT},
                        {"select.A", cell.evaluation_gate.select.A}, {"select.B", cell.evaluation_gate.select.B},
                        {"select.Y", cell.evaluation_gate.select.Y},
                        {"selected.A", cell.evaluation_gate.selected.A},
                        {"selected.Y", cell.evaluation_gate.selected.Y}, {"tracking.A", cell.tracking.A},
                        {"tracking.Y", cell.tracking.Y}, {"tracking.BIAS", cell.tracking.BIAS},
                        {"sampler.IN", cell.sampler.IN}, {"sampler.OUT", cell.sampler.OUT}, {"sampler.CLOCK", cell.sampler.CLOCK},
                        {"decision.IN", cell.decision.IN}, {"decision.CORE_DECISION", cell.decision.CORE_DECISION},
                        {"decision.BIAS", cell.decision.BIAS}, {"decision.CLEAR", cell.decision.CLEAR},
                        {"decision.EVALUATE", cell.decision.EVALUATE}})
                    anchor(s + "." + item.name, item.terminal);
                for (unsigned bit = 0; bit < 8; ++bit) {
                    port("TRIAL_" + number + "_" + std::to_string(bit), chip.TRIAL[lane][slice][bit],
                         "input", index(index("trial_out", 16 * lane + slice), bit));
                    anchor(index(s + ".decision.TRIAL", bit), cell.decision.TRIAL[bit]);
                    anchor(index(s + ".dac.A", bit), cell.decision.dac.controls[bit].A);
                }
            }
        }
        verilog_a::detail::write_file(destination / "RepeaterAnalog.va",
            verilog_a::render_simulation_model(sim, "RepeaterAnalog", ports));
        std::ofstream json(destination / "electrical.json");
        json << std::setprecision(17);
        json << "{\n\"full_chip_qualified\": false,\n\"ports\": [\n";
        for (unsigned i = 0; i < bindings.size(); ++i) {
            const auto& b = bindings[i];
            json << (i ? ",\n" : "") << "{\"name\":\"" << b.name << "\",\"direction\":\"" << b.direction
                 << "\",\"digital\":\"" << b.digital << "\",\"node\":" << b.node << '}';
        }
        json << "\n],\n\"anchors\": {\n";
        bool first = true;
        for (const auto& a : anchors) {
            json << (first ? "" : ",\n") << '"' << a.first << "\":" << a.second;
            first = false;
        }
        json << "\n},\n\"components\": [\n";
        first = true;
        for (const auto& component : sim.components()) {
            int status = 0;
            std::unique_ptr<char, decltype(&std::free)> demangled(
                abi::__cxa_demangle(component.class_name.c_str(), nullptr, nullptr, &status), &std::free);
            json << (first ? "" : ",\n") << "{\"id\":" << component.id << ",\"parent\":";
            if (component.parent == no_component) json << "null";
            else json << component.parent;
            json << ",\"class\":" << std::quoted(status == 0 ? demangled.get() : component.class_name.c_str()) << '}';
            first = false;
        }
        json << "\n],\n\"elements\": [\n";
        first = true;
        for (const auto& e : sim.elements) {
            const char* kind = e.kind == law::nmos ? "nmos" : e.kind == law::pmos ? "pmos" :
                               e.kind == law::resistor ? "resistor" : e.kind == law::capacitor ? "capacitor" : nullptr;
            if (!kind) throw std::runtime_error("unexpected law in physical electrical partition");
            json << (first ? "" : ",\n") << "{\"kind\":\"" << kind << "\",\"owner\":" << e.component
                 << ",\"file\":" << std::quoted(e.file) << ",\"line\":" << e.line
                 << ",\"r\":" << e.r << ",\"c\":" << e.c << ",\"beta\":" << e.beta
                 << ",\"threshold\":" << e.threshold << ",\"nodes\":[";
            first = false;
            unsigned position = 0;
            for (const auto node : verilog_a::detail::terminals(e))
                json << (position++ ? "," : "") << sim.root(node);
            json << "]}";
        }
        json << "\n]}\n";
        if (!json) throw std::runtime_error("failed writing electrical topology");
        std::cout << "Exported " << sim.elements.size() << " circuit laws, " << bindings.size() << " terminals\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
