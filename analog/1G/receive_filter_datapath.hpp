#pragma once
#include "receive_filter_pipeline.hpp"

// PHY-facing receive filter. The underlying class retains coefficient probes
// for its full-state unit contract; this interface carries samples and control.
class ReceiveFilter : public Module {
public:
    _PORT(u<8>) sample_in;
    _PORT(u<12>) local_in;
    _PORT(u<3>) desired_in;
    _PORT(bool) train_in;
    _PORT(bool) advance_in;
    _PORT(u<8>) sample_out = _ASSIGN(filter.sample_out());
    _PORT(bool) valid_out = _ASSIGN(filter.valid_out());
    _PORT(bool) fault_out = _ASSIGN(filter.fault_out());

private:
    PipelinedReceiveFilter filter;

public:
    void _assign() {
        filter.sample_in = _ASSIGN(sample_in());
        filter.local_in = _ASSIGN(local_in());
        filter.desired_in = _ASSIGN(desired_in());
        filter.train_in = _ASSIGN(train_in());
        filter.advance_in = _ASSIGN(advance_in());
        filter._assign();
    }
    void _work(bool reset) {
        filter._work(reset);
    }
    void _strobe() {
        filter._strobe();
    }
};
