#pragma once
#include "cpphdl_graph.h"
#include <cmath>
#include <cctype>

namespace cpphdl::synth {
struct BlackBoxSpec {
    std::string module;
    uint64_t delayPs = 0;
};
// The annotation is an explicit external combinational implementation contract.
// Each scalar argument occupies a 64-bit slot, first argument in the low bits.
inline std::optional<BlackBoxSpec> blackBoxSpec(const std::vector<std::string>& annotations) {
    std::optional<BlackBoxSpec> result;
    const std::string prefix = "CPPHDL_BLACKBOX=";
    for (const auto& text : annotations) if (text.find(prefix) == 0) {
        if (result) throw std::runtime_error("duplicate CPPHDL_BLACKBOX annotation");
        auto split = text.find(':', prefix.size());
        if (split == std::string::npos) throw std::runtime_error("CPPHDL_BLACKBOX requires module:delay_ns");
        auto module = text.substr(prefix.size(), split-prefix.size());
        if (module.empty() || (!std::isalpha(static_cast<unsigned char>(module[0])) && module[0] != '_') ||
            std::any_of(module.begin(), module.end(), [](unsigned char c) { return !std::isalnum(c) && c != '_'; }))
            throw std::runtime_error("invalid blackbox module name");
        size_t used = 0;
        auto suffix = text.substr(split+1);
        double delay = std::stod(suffix, &used);
        if (used != suffix.size() || !std::isfinite(delay) || delay < 0 || delay > 1e9)
            throw std::runtime_error("invalid blackbox delay");
        result = BlackBoxSpec{module, uint64_t(std::ceil(delay*1000))};
    }
    return result;
}
inline double blackBoxDelay(const graph::Node& n) {
    auto delay = graph::number(n.right);
    if (!delay || n.left.empty() || n.left.size()%64 || !n.select.empty() || n.name.empty())
        throw std::runtime_error("invalid blackbox graph node");
    return double(*delay)/1000;
}
}
