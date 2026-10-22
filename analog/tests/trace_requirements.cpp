#include <analog.hpp>
#include "../examples/feedback_checks.hpp"
#include <iostream>

using namespace analog;
using namespace analog::literals;

int main() {
    int failures = 0;
    auto check = [&](bool pass, const char* name) {
        if (!pass) {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    for (int outcome : {0, 1, 2}) {
        simulation sim;
        electrical ground, signal;
        sim.reference(ground);
        signal = sim_voltage_source(ground, 0_V, 1_us, 1_V);
        sim.observe("signal", signal, ground);
        bool saw_history = false;
        sim.require_trace("history", [&](const auto& trace) -> std::optional<bool> {
            saw_history = trace.size() >= 3 && std::abs(*trace.front().values[0]) < 1e-8 &&
                          std::abs(*trace.back().values[0] - 1) < 1e-8;
            return outcome == 2 ? std::nullopt : std::optional<bool>(outcome == 0 && saw_history);
        });
        sim.run(2_us, 1_us);
        check(saw_history, "predicate receives complete trace");
        check(sim.status() == (outcome == 0   ? "sampled_pass"
                               : outcome == 1 ? "failed"
                                              : "unverified"),
              "pass, failure and missing evidence affect overall status");
        bool frozen = false;
        try {
            sim.require_trace("late declaration", [](const auto&) { return true; });
        } catch (const diagnostic& d) {
            frozen = d.code == "E_ELABORATION";
        }
        check(frozen, "trace requirements freeze with the circuit");
    }
    {
        simulation sim;
        electrical ground, signal;
        sim.reference(ground);
        signal = sim_voltage_source(ground, 1_V);
        sim.assume_voltage("envelope", signal, ground, 0_V, .5_V);
        bool called = false;
        sim.require_trace("conditional guarantee", [&](const auto&) {
            called = true;
            return true;
        });
        sim.run(1_us, 1_us);
        check(sim.status() == "out_of_contract" && !called, "violated assumptions suppress trace guarantees");
    }
    {
        simulation sim;
        bool rejected = false;
        try {
            sim.require_trace("empty", {});
        } catch (const diagnostic& d) {
            rejected = d.code == "E_VALUE";
        }
        check(rejected, "empty predicate rejected");
    }
    {
        simulation sim;
        electrical ground, signal;
        sim.reference(ground);
        signal = sim_voltage_source(ground, 1_V);
        sim.require_final_voltage("failed final", signal, ground, 0_V, .5_V);
        sim.require_trace("passed trace", [](const auto&) { return true; });
        sim.run(1_us, 1_us);
        check(sim.status() == "failed", "a passing trace does not erase a failed final requirement");
    }
    std::vector<sample> trace{{0, {0.0}, {}}, {1, {1.0}, {}}, {2, {0.0}, {}}, {3, {1.0}, {}}};
    auto edges = feedback_checks::rising_edges(trace, 0, .5, 0, 3);
    check(edges.size() == 2 && edges[0] == .5 && edges[1] == 2.5, "crossing interpolation");
    check(!feedback_checks::frequency(edges), "frequency measurement rejects too few periods");
    check(!feedback_checks::window(trace, 0, 4, 5), "empty window gives no evidence");
    trace[1].values[0] = std::nullopt;
    check(!feedback_checks::window(trace, 0, 0, 3), "unknown sample gives no evidence");
    trace[1].values[0] = std::numeric_limits<double>::quiet_NaN();
    check(!feedback_checks::window(trace, 0, 0, 3), "nonfinite sample gives no evidence");
    return failures ? 1 : 0;
}
