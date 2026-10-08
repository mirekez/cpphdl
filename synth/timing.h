#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Nanoseconds. Deliberately small, conservative estimates, not a cell library.
struct DelayModel {
    double gate = 0.05, mux = 0.10, carry = 0.06;
    double clockToQ = 0.10, setup = 0.05;
    double memoryBase = 0.50, memoryLevel = 0.08;
};
struct TimingReport {
    double worst = 0;
    std::string endpoint;
    std::vector<double> arrival;
};
double cellDelay(const graph::Graph&, const graph::Node&, const DelayModel&);
TimingReport estimateTiming(graph::Graph&, const DelayModel& = {}, const std::string& scope = {});
}
