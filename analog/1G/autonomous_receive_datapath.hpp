#pragma once
#include "autonomous_receive_filter.hpp"

// Physical lane interface for autonomous echo and receive adaptation.
// Coefficient probes remain on the underlying filter's unit-test interface;
// the chip routes sample, local transmit reference and acquisition mode.
class AutonomousReceiveDatapath : public Module {
public:
    _PORT(u<8>) sample_in;
    _PORT(u<12>) local_in;
    _PORT(u<2>) mode_in;
    _PORT(bool) advance_in;
    _PORT(u<8>) sample_out;
    _PORT(bool) valid_out;
    _PORT(bool) fault_out;

private:
    AutonomousReceiveFilter filter;

public:
    void _assign() {
        filter.sample_in = _ASSIGN(sample_in());
        filter.local_in = _ASSIGN(local_in());
        filter.mode_in = _ASSIGN(mode_in());
        filter.advance_in = _ASSIGN(advance_in());
        sample_out = _ASSIGN(filter.sample_out());
        valid_out = _ASSIGN(filter.valid_out());
        fault_out = _ASSIGN(filter.fault_out());
        filter._assign();
    }
    void _work(bool reset) { filter._work(reset); }
    void _strobe() { filter._strobe(); }
};
