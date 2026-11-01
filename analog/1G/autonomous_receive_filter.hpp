#pragma once
#include "receive_filter_pipeline_core.hpp"

// Acquire local echo without a supplied far-end symbol. After acquisition,
// sliced idle/data decisions refine the same filter. Mode changes belong to
// accepted samples and follow their features through the update pipeline.
class AutonomousReceiveFilter : public Module {
public:
    static constexpr unsigned OUTPUT_LATENCY = ReceiveFilterPipelineCore::OUTPUT_LATENCY;
    static constexpr unsigned UPDATE_LATENCY = ReceiveFilterPipelineCore::UPDATE_LATENCY;
    _PORT(u<8>) sample_in;
    _PORT(u<12>) local_in;
    // 0 holds coefficients, 1 acquires echo, 2 adapts PAM3 idles, 3 adapts PAM5.
    _PORT(u<2>) mode_in;
    _PORT(bool) advance_in;
    _PORT(u<8>) sample_out = _ASSIGN(core.sample_out());
    _PORT(bool) valid_out = _ASSIGN(core.valid_out());
    _PORT(bool) fault_out = _ASSIGN(core.fault_out());
    _PORT(u<18>) coefficient_out[24];

private:
    ReceiveFilterPipelineCore core;

public:
    void _assign() {
        core.sample_in = _ASSIGN(sample_in());
        core.local_in = _ASSIGN(local_in());
        core.desired_in = _ASSIGN(2);
        core.train_in = _ASSIGN(mode_in() != 0);
        core.acquisition_in = _ASSIGN(mode_in());
        core.advance_in = _ASSIGN(advance_in());
        for (unsigned i = 0; i < 24; ++i)
            coefficient_out[i] = _ASSIGN_I(core.coefficient_out[i]());
        core._assign();
    }
    void _work(bool reset) { core._work(reset); }
    void _strobe() { core._strobe(); }
};
