#include "Verilog.h"
#include "retiming.h"
#include "KeepBoxes.h"
#include "Mapping.h"
#include <filesystem>
#include <cstdio>
#include <cmath>

cpphdl::graph::Graph makeGraph();

int main(int argc, char** argv) {
    if (argc != 9) return 2;
    try {
        auto graph = makeGraph();
        cpphdl::synth::DelayModel model;
        double scale = std::stod(argv[8]);
        if (!std::isfinite(scale) || scale <= 0) throw std::runtime_error("delay scale must be positive");
        model.gate *= scale; model.mux *= scale; model.carry *= scale;
        model.clockToQ *= scale; model.setup *= scale; model.memoryBase *= scale; model.memoryLevel *= scale;
        std::vector<cpphdl::synth::RetimingRule> rules;
        if (std::string(argv[5]) != "none") rules.push_back({argv[5], std::stod(argv[6]), argv[7]});
        else for (const auto& attribute : graph.attributes) if (attribute.name == "retiming") {
            auto colon = attribute.value.find(':');
            if (colon == std::string::npos) throw std::runtime_error("retiming annotation requires mode:period_ns");
            size_t used = 0;
            auto period = std::stod(attribute.value.substr(colon + 1), &used);
            if (used != attribute.value.size() - colon - 1) throw std::runtime_error("invalid annotated period");
            rules.push_back({attribute.value.substr(0, colon), period, attribute.scope});
        }
        for (size_t a = 0; a < rules.size(); ++a) for (size_t b = a + 1; b < rules.size(); ++b)
            if (rules[a].scope.empty() || rules[b].scope.empty() || rules[a].scope == rules[b].scope ||
                rules[a].scope.find(rules[b].scope + ".") == 0 || rules[b].scope.find(rules[a].scope + ".") == 0)
                throw std::runtime_error("overlapping retiming scopes");
        std::ofstream report(argv[3]);
        report << "{\"estimate_only\":true,\"rules\":[";
        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            auto result = cpphdl::synth::retime(graph, rule, model);
            report << (i ? "," : "") << "{\"mode\":" << std::quoted(rule.mode) << ",\"scope\":" << std::quoted(rule.scope)
                   << ",\"target_ns\":" << result.target << ",\"before_ns\":" << result.before
                   << ",\"after_ns\":" << result.after << ",\"moved_boundaries\":" << result.moved
                   << ",\"inserted_register_bits\":" << result.insertedBits << ",\"added_latency\":" << result.addedLatency
                   << ",\"initiation_interval\":" << result.initiationInterval
                   << ",\"feedback_scheduled\":" << (result.feedbackScheduled ? "true" : "false")
                   << ",\"target_met\":" << (result.met ? "true" : "false") << "}";
        }
        report << "],\"worst_ns\":" << cpphdl::synth::estimateTiming(graph, model).worst;
        cpphdl::synth::KeepBoxes boxes(graph);
        report << ",\"keep_boxes\":[";
        bool comma = false;
        for (const auto& box : boxes.boxes) if (!box.outputs.empty()) {
            report << (comma ? "," : "") << "{\"module\":" << std::quoted(box.module)
                   << ",\"scope\":" << std::quoted(box.scope) << ",\"delay_ns\":" << box.delay << "}";
            comma = true;
        }
        report << "],\"streaming_regions\":[";
        comma = false;
        for (const auto& pipeline : graph.pipelines) {
            report << (comma ? "," : "") << "{\"scope\":" << std::quoted(pipeline.scope)
                   << ",\"hls_stages\":" << pipeline.stages << ",\"latency\":" << pipeline.latency
                   << ",\"initiation_interval\":1,\"feedback_policy\":\"floating\"}";
            comma = true;
        }
        report << "]}\n";
        if (!report) throw std::runtime_error("cannot write timing report");
        graph.writeCpp(argv[4]);
        cpphdl::synth::emitVerilog(graph, argv[1], argv[2]);
        auto directory = std::filesystem::path(argv[1]).parent_path();
        auto gates = cpphdl::synth::mapGates(graph);
        cpphdl::synth::emitVerilog(gates, (directory / "gates.v").string(), argv[2]);
        cpphdl::synth::writeGateReport(gates, (directory / "gates.json").string(), argv[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CppHDL synthesis: %s\n", error.what());
        return 1;
    }
}
