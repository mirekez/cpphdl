#pragma once
#include "timing.h"

namespace cpphdl::synth {
struct RetimingRule {
    std::string mode;
    double period = 0;
    std::string scope; // Empty means the entire design; otherwise an instance path.
};
struct RetimingReport {
    double before = 0, after = 0, target = 0;
    unsigned moved = 0, insertedBits = 0, addedLatency = 0;
    bool met = false;
};
RetimingReport retime(graph::Graph&, const RetimingRule&, const DelayModel& = {});
}
