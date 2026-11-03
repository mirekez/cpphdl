#include "../reference_clock_generator.hpp"
#include <VerilogA/export.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace analog;

// Retain observation identities from C++ elaboration instead of guessing
// private node numbers from the transistor graph after layout generation.
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected export directory");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        simulation sim;
        one_g::ihp_sg13g2::reference_clock_generator clock;
        clock.name("OneGIHPReferenceClockGenerator");
        clock.elaborate();
        const auto root = std::filesystem::canonical(__FILE__).parent_path().parent_path().parent_path().parent_path();
        for (auto& element : sim.elements)
            element.file = std::filesystem::relative(std::filesystem::weakly_canonical(element.file), root).generic_string();
        using verilog_a::named_port;
        const std::vector<named_port> ports{{"Vdd", clock.Vdd}, {"Vss", clock.Vss},
            {"Vlogic", clock.Vlogic}, {"REF", clock.REF}, {"CLEAR", clock.CLEAR},
            {"OUT", clock.OUT}, {"OUT_N", clock.OUT_N}};
        const auto node_name = [&](const electrical& terminal) {
            const auto node = sim.root(terminal.id());
            for (const auto& port : ports)
                if (sim.root(port.terminal.id()) == node)
                    return port.name;
            return std::string("cpphdl_n") + std::to_string(node);
        };
        verilog_a::detail::write_file(destination / "reference.va",
            verilog_a::render_simulation_model(sim, "OneGIHPReferenceClockGenerator", ports));
        const std::vector<named_port> probes{{"LOW", clock.multiplier.OUT}, {"OUT", clock.OUT},
            {"OUT_N", clock.OUT_N}, {"DIVIDED", clock.multiplier.DIVIDED}, {"REF", clock.REF},
            {"CONTROL", clock.multiplier.CONTROL}, {"VLOGIC", clock.Vlogic}, {"VDD", clock.Vdd},
            {"BIAS", clock.translator.BIAS}, {"UP", clock.multiplier.detector.UP},
            {"DOWN", clock.multiplier.detector.DOWN}, {"PREDRIVE", clock.predriver.Y},
            {"BUFFERED", clock.driver.Y}};
        std::ofstream output(destination / "probes.json");
        output << "{\n";
        bool first = true;
        for (const auto& probe : probes) {
            const auto name = node_name(probe.terminal);
            if (!first)
                output << ",\n";
            output << "  \"" << probe.name << "\": \"" << name << "\"";
            first = false;
        }
        output << "\n}\n";
        if (!output)
            throw std::runtime_error("incomplete clock observation export");
        // Physical placement follows the actual IC ownership of each device.
        // Keep master/slave storage together instead of inferring hierarchy
        // from private node numbers or an assumed transistor emission order.
        std::ofstream hierarchy(destination / "hierarchy.json");
        hierarchy << "{\n";
        bool first_group = true;
        std::map<std::string, std::set<std::size_t>> group_owners;
        const std::vector<std::pair<std::string, cmos_dff*>> groups{
            {"divider", &clock.multiplier.feedback.storage},
            {"reference_detector", &clock.multiplier.detector.reference},
            {"feedback_detector", &clock.multiplier.detector.feedback}};
        for (const auto& [name, storage] : groups) {
            std::set<std::size_t> owners{storage->id(), storage->clock_inverter.id()};
            for (auto* latch : {&storage->master, &storage->slave}) {
                for (const IC* component : std::vector<const IC*>{latch,
                        &latch->data_inverter, &latch->clear_inverter,
                        &latch->enable_inverter, &latch->qualify_enable,
                        &latch->set_gate, &latch->reset_gate, &latch->storage,
                        &latch->storage.clear_inverter,
                        &latch->storage.gates[0], &latch->storage.gates[1]})
                    owners.insert(component->id());
            }
            group_owners[name] = owners;
            if (!first_group)
                hierarchy << ",\n";
            hierarchy << "  \"" << name << "\": [";
            bool first_device = true;
            for (std::size_t index = 0; index < sim.elements.size(); ++index) {
                if (owners.count(sim.elements[index].component)) {
                    if (!first_device)
                        hierarchy << ", ";
                    hierarchy << index;
                    first_device = false;
                }
            }
            hierarchy << "]";
            first_group = false;
        }
        hierarchy << "\n}\n";
        if (!hierarchy)
            throw std::runtime_error("incomplete clock hierarchy export");

        // Process realizations replace whole owned ICs through explicit ports.
        // Keep this separate from placement groups: the detector includes its
        // reset network as well as both flip-flops, and owns unmerged storage.
        auto detector_owners = group_owners.at("reference_detector");
        detector_owners.insert(group_owners.at("feedback_detector").begin(),
                               group_owners.at("feedback_detector").end());
        for (const IC* component : std::vector<const IC*>{&clock.multiplier.detector,
                &clock.multiplier.detector.both, &clock.multiplier.detector.reset,
                &clock.multiplier.detector.clear_inverter})
            detector_owners.insert(component->id());
        auto divider_owners = group_owners.at("divider");
        divider_owners.insert(clock.multiplier.feedback.id());
        std::ofstream realizations(destination / "realizations.json");
        realizations << "{\n";
        const auto block = [&](const std::string& name, const std::set<std::size_t>& owners,
                               const std::vector<named_port>& terminals) {
            realizations << "  \"" << name << "\": {\"ports\": {";
            bool first = true;
            for (const auto& terminal : terminals) {
                if (!first)
                    realizations << ", ";
                realizations << "\"" << terminal.name << "\": \""
                             << node_name(terminal.terminal) << "\"";
                first = false;
            }
            realizations << "}, \"elements\": [";
            first = true;
            for (std::size_t index = 0; index < sim.elements.size(); ++index) {
                if (!owners.count(sim.elements[index].component))
                    continue;
                if (!first)
                    realizations << ", ";
                realizations << index;
                first = false;
            }
            realizations << "]}";
        };
        const auto& detector = clock.multiplier.detector;
        block("detector", detector_owners, {{"VDD", detector.Vdd}, {"VSS", detector.Vss},
            {"REF", detector.REF}, {"FEEDBACK", detector.FEEDBACK}, {"CLEAR", detector.CLEAR},
            {"UP", detector.UP}, {"UP_N", detector.UP_N}, {"DOWN", detector.DOWN}, {"DOWN_N", detector.DOWN_N}});
        realizations << ",\n";
        const auto& divider = clock.multiplier.feedback;
        block("divider", divider_owners, {{"VDD", divider.Vdd}, {"VSS", divider.Vss},
            {"CLOCK", divider.CLOCK}, {"CLEAR", divider.CLEAR}, {"Q", divider.Q}, {"Q_N", divider.Q_N}});
        realizations << "\n}\n";
        if (!realizations)
            throw std::runtime_error("incomplete clock realization ownership export");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
