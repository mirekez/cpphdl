#pragma once
#include "common.hpp"

// Testbench measurements only. All feedback in the examples is made from circuit devices.
namespace feedback_checks {
using namespace analog;
struct statistics {
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    double mean = 0;
    std::size_t count = 0;
};
inline std::optional<statistics> window(const std::vector<sample>& trace, std::size_t column, double start,
                                        double end) {
    statistics result;
    for (const auto& row : trace) {
        if (row.time < start || row.time > end)
            continue;
        if (column >= row.values.size() || !row.values[column] || !std::isfinite(*row.values[column]))
            return std::nullopt;
        double value = *row.values[column];
        result.low = std::min(result.low, value);
        result.high = std::max(result.high, value);
        result.mean += value;
        ++result.count;
    }
    if (result.count < 2)
        return std::nullopt;
    result.mean /= result.count;
    return result;
}
inline void bounds(simulation& sim, std::string name, std::size_t column, double start, double end, double low,
                   double high) {
    sim.require_trace(name, [=](const auto& trace) -> std::optional<bool> {
        auto result = window(trace, column, start, end);
        if (!result)
            return std::nullopt;
        std::cout << name << ": min=" << result->low << ", max=" << result->high << ", mean=" << result->mean << '\n';
        return result->low >= low && result->high <= high;
    });
}
inline std::vector<double> rising_edges(const std::vector<sample>& trace, std::size_t column, double threshold,
                                        double start, double end) {
    std::vector<double> result;
    for (std::size_t i = 1; i < trace.size(); ++i) {
        const auto& a = trace[i - 1];
        const auto& b = trace[i];
        if (column >= a.values.size() || column >= b.values.size() || !a.values[column] || !b.values[column])
            continue;
        double av = *a.values[column], bv = *b.values[column];
        if (!std::isfinite(av) || !std::isfinite(bv) || av >= threshold || bv < threshold || b.time <= a.time)
            continue;
        double time = a.time + (b.time - a.time) * (threshold - av) / (bv - av);
        if (time >= start && time <= end)
            result.push_back(time);
    }
    return result;
}
inline std::optional<double> frequency(const std::vector<double>& edges) {
    if (edges.size() < 20 || edges.back() <= edges.front())
        return std::nullopt;
    return (edges.size() - 1) / (edges.back() - edges.front());
}
inline void lock(simulation& sim, double expected_frequency) {
    sim.require_trace(
        "late frequency, cycle count and unwrapped phase lock", [=](const auto& trace) -> std::optional<bool> {
            auto reference = rising_edges(trace, 0, .6, .01, .015);
            auto output = rising_edges(trace, 1, .6, .01, .015);
            auto f = frequency(output);
            if (!f || reference.size() < 20)
                return false; // A stopped oscillator must fail, rather than disappear from the measurement.
            double period = 1 / expected_frequency;
            auto first = std::min_element(reference.begin(), reference.end(), [&](double a, double b) {
                return std::abs(a - output.front()) < std::abs(b - output.front());
            });
            std::size_t offset = std::size_t(first - reference.begin());
            double phase_low = 1e9, phase_high = -1e9;
            for (std::size_t i = 0; i < output.size() && i + offset < reference.size(); ++i) {
                double phase = (output[i] - reference[i + offset]) / period;
                phase_low = std::min(phase_low, phase);
                phase_high = std::max(phase_high, phase);
            }
            bool periods = true;
            for (std::size_t i = 1; i < output.size(); ++i)
                periods &= std::abs((output[i] - output[i - 1]) / period - 1) < .05;
            auto count_error = std::abs(double(output.size()) - double(reference.size()));
            std::cout << "PLL: Hz=" << *f << ", phase cycles=[" << phase_low << ", " << phase_high << "]\n";
            return std::abs(*f / expected_frequency - 1) < .01 && count_error <= 1 && periods &&
                   phase_high - phase_low < .05 && phase_low > -.4 && phase_high < .4;
        });
}
inline bool scenario_is(const std::string& scenario, std::initializer_list<const char*> allowed) {
    for (auto name : allowed)
        if (scenario == name)
            return true;
    std::cerr << "Unknown scenario: " << scenario << '\n';
    return false;
}
} // namespace feedback_checks
