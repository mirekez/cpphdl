// Simulation adapter: the native C++RTL controller runs on XSPICE clock events.
// Analog voltages remain in ngspice; adc_bridge/dac_bridge handle host signals.
#ifdef CPPHDL_ADC_COMPACT
#include "../../adc_receive_bank.hpp"
using Controller = ADCReceiveBankController<13>;
#else
#include "../../adc_receive.hpp"
using Controller = ADCReceiveController;
#endif
#include <ngspice/cmtypes.h>
#include <ngspice/cosim.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

long _system_clock = -1;

namespace {
[[noreturn]] void fail(const char* message);

struct host {
    Controller controller;
    unsigned count = 0, launches = 0, feedback = 0;
    bool launch = false, precharging = true;
    int clock = -1;
    Digital_State_t previous_clock = ZERO;
    std::vector<Digital_State_t> inputs;
    std::vector<unsigned char> outputs;
    std::ofstream controls, events;

    void record() {
        unsigned acquire = 0, convert = 0;
        ++_system_clock;
        for (unsigned i = 0; i < count; ++i) {
            acquire |= unsigned(controller.acquire_out[i]()) << i;
            convert |= unsigned(controller.convert_out[i]()) << i;
        }
        controls << clock << ',' << unsigned(controller.data_out()) << ',' << bool(controller.valid_out()) << ','
                 << unsigned(controller.slice_out()) << ',' << bool(controller.fault_out()) << ',' << acquire << ','
                 << convert;
        for (unsigned i = 0; i < count; ++i)
            controls << ',' << unsigned(controller.trial_out[i]());
        controls << ',' << unsigned(controller.normalized_out()) << ',' << bool(controller.normalized_valid_out()) << ','
                 << unsigned(controller.normalized_slice_out()) << ',' << bool(controller.clipped_out()) << '\n';
        controls.flush();
        if (!controls)
            fail("cannot write XSPICE controller evidence");
    }

    void initialize(const co_info& info) {
        if (info.sim_argc != 4)
            throw std::runtime_error("expected slices, bursts, controls CSV and event CSV");
        count = unsigned(std::stoul(info.sim_argv[0]));
        const unsigned bursts = unsigned(std::stoul(info.sim_argv[1]));
#ifdef CPPHDL_ADC_COMPACT
        if ((count != 1 && count != 4 && count != 13) || (bursts != 1 && bursts != 2) || (bursts == 2 && count != 13))
#else
        if ((count != 1 && count != 4 && count != 16) || (bursts != 1 && bursts != 2) || (bursts == 2 && count != 16))
#endif
            throw std::runtime_error("invalid XSPICE controller experiment");
        launches = count * bursts;
        inputs.assign(count + 1, UNKNOWN);
        outputs.assign(9 * count, 2);
        controls.open(info.sim_argv[2]);
        events.open(info.sim_argv[3]);
        if (!controls || !events)
            throw std::runtime_error("cannot open XSPICE controller evidence");
        controls << "clock,data,valid,slice,fault,acquire,convert";
        for (unsigned i = 0; i < count; ++i)
            controls << ",trial" << i;
        controls << ",normalized,normalized_valid,normalized_slice,clipped\n";
        events << "clock,time_s,active_mask,feedback\n" << std::setprecision(17);
#ifdef CPPHDL_ADC_COMPACT
        controller.precharge_in = _ASSIGN(precharging);
#endif
        controller.enable_in = _ASSIGN(true);
        controller.sample_enable_in = _ASSIGN(launch);
        controller.comparisons_in = _ASSIGN(feedback);
#ifdef CPPHDL_ADC_COMPACT
        controller.offset_in = _ASSIGN(112);
#else
        controller.offset_in = _ASSIGN(128);
#endif
        controller.gain_in = _ASSIGN(424);
        controller._assign();
        ++_system_clock;
        controller._work(true);
        controller._strobe();
        record();
    }
};

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "C++RTL XSPICE controller failed: %s\n", message);
    std::exit(1);
}

void input(co_info* info, unsigned index, Digital_t* value) {
    auto& context = *static_cast<host*>(info->handle);
    if (index >= context.inputs.size())
        fail("input index exceeds declared interface");
    context.inputs[index] = value->strength <= RESISTIVE ? value->state : UNKNOWN;
}

void step(co_info* info) {
    auto& context = *static_cast<host*>(info->handle);
    const auto clock_input = context.inputs[0];
    unsigned mask = 0;
    if (clock_input != UNKNOWN && clock_input != context.previous_clock) {
        context.previous_clock = clock_input;
        if (clock_input == ONE) {
            context.precharging = false;
            ++_system_clock;
            for (unsigned i = 0; i < context.count; ++i)
                mask |= unsigned(context.controller.convert_out[i]()) << i;
            context.feedback = 0;
            for (unsigned i = 0; i < context.count; ++i)
                if (mask & (1u << i)) {
                    if (context.inputs[i + 1] == UNKNOWN)
                        fail("unresolved core-domain decision on a sampling edge");
                    context.feedback |= unsigned(context.inputs[i + 1] == ONE) << i;
                }
            ++context.clock;
            context.launch = unsigned(context.clock) < context.launches;
            context.events << context.clock << ',' << info->vtime << ',' << mask << ',' << context.feedback << '\n';
            context.events.flush();
            if (!context.events)
                fail("cannot write XSPICE bridge-event evidence");
            ++_system_clock;
            context.controller._work(false);
            context.controller._strobe();
            context.record();
        }
    }
    ++_system_clock;
    for (unsigned i = 0; i < context.count; ++i)
        for (unsigned bit = 0; bit < 9; ++bit) {
            const unsigned index = i * 9 + bit;
            const unsigned value = bit == 8 ? unsigned(context.controller.acquire_out[i]()) :
                1 - ((unsigned(context.controller.trial_out[i]()) >> bit) & 1);
            if (context.outputs[index] != value) {
                Digital_t output{value ? ONE : ZERO, STRONG};
                context.outputs[index] = value;
                info->out_fn(info, index, &output);
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
        info->in_count = context->count + 1;
        info->out_count = 9 * context->count;
        info->inout_count = 0;
        info->method = After_input;
        info->in_fn = input;
        info->step = step;
        info->cleanup = cleanup;
    } catch (const std::exception& error) {
        fail(error.what());
    }
}
