// Simulation host for the actual two-clock calibration controller. XSPICE
// supplies reference and achieved electrical oscillator edges independently.
// The extra output drives the connected physical admission latch.
#include "../../processing_clock_control.hpp"
#include <ngspice/cmtypes.h>
#include <ngspice/cosim.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>

long _system_clock = -1;

namespace {
[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Processing-clock co-simulation failed: %s\n", message);
    std::exit(1);
}

struct host {
    ProcessingClockControl controller;
    std::array<Digital_State_t, 2> inputs{UNKNOWN, UNKNOWN}, previous{ZERO, ZERO};
    std::array<unsigned, 18> outputs;
    std::ofstream events;

    void initialize(const co_info& info) {
        if (info.sim_argc != 1)
            throw std::runtime_error("expected clock-event evidence path");
        events.open(info.sim_argv[0]);
        if (!events)
            throw std::runtime_error("cannot open clock-event evidence");
        events << "time_s,ref_edge,pcs_edge,trim,clear,ready_ref,ready_pcs,fault,measured\n" << std::setprecision(17);
        outputs.fill(2);
        controller.enable_in = _ASSIGN(true);
        controller._assign();
        ++_system_clock;
        controller._reset_pos_ref_clk();
        controller._reset_pos_pcs_clk();
        controller._strobe_ref_clk();
        controller._strobe_pcs_clk();
    }
};

void input(co_info* info, unsigned index, Digital_t* value) {
    if (index >= 2)
        fail("clock input index exceeds the two-clock interface");
    auto& context = *static_cast<host*>(info->handle);
    context.inputs[index] = value->strength <= RESISTIVE ? value->state : UNKNOWN;
}

void step(co_info* info) {
    auto& context = *static_cast<host*>(info->handle);
    bool rising[2]{};
    for (unsigned index = 0; index < 2; ++index) {
        // Preserve the last known rail through the bridge's threshold band,
        // matching the native electrical Schmitt clock interpretation.
        if (context.inputs[index] != UNKNOWN) {
            rising[index] = context.previous[index] == ZERO && context.inputs[index] == ONE;
            context.previous[index] = context.inputs[index];
        }
    }
    if (rising[0] || rising[1]) {
        ++_system_clock;
        if (rising[0]) context.controller._work_ref_clk(false);
        if (rising[1]) context.controller._work_pcs_clk(false);
        if (rising[0]) context.controller._strobe_ref_clk();
        if (rising[1]) context.controller._strobe_pcs_clk();
        ++_system_clock;
        const unsigned trim = ((unsigned(context.controller.coarse_n_out()) ^ 255) << 8) |
                               (unsigned(context.controller.fine_n_out()) ^ 255);
        context.events << info->vtime << ',' << rising[0] << ',' << rising[1] << ',' << trim << ','
                       << context.controller.clear_out() << ',' << context.controller.ready_ref_out() << ','
                       << context.controller.ready_pcs_out() << ',' << context.controller.fault_out() << ','
                       << unsigned(context.controller.measured_edges_out()) << '\n';
        context.events.flush();
        if (!context.events)
            fail("cannot retain processing clock events");
    }
    ++_system_clock;
    for (unsigned bit = 0; bit < context.outputs.size(); ++bit) {
        const unsigned value = bit == 17 ? unsigned(context.controller.ready_pcs_out()) : bit == 16 ? unsigned(context.controller.clear_out()) :
            bit < 8 ? ((unsigned(context.controller.coarse_n_out()) >> bit) & 1) :
                      ((unsigned(context.controller.fine_n_out()) >> (bit - 8)) & 1);
        if (context.outputs[bit] != value) {
            Digital_t output{value ? ONE : ZERO, STRONG};
            context.outputs[bit] = value;
            info->out_fn(info, bit, &output);
        }
    }
}

void cleanup(co_info* info) {
    delete static_cast<host*>(info->handle);
    info->handle = nullptr;
}
} // namespace

extern "C" void Cosim_setup(co_info* info) {
    try {
        auto* context = new host;
        info->handle = context;
        context->initialize(*info);
        info->in_count = 2;
        info->out_count = 18;
        info->inout_count = 0;
        info->method = After_input;
        info->in_fn = input;
        info->step = step;
        info->cleanup = cleanup;
    } catch (const std::exception& error) {
        fail(error.what());
    }
}
