#pragma once
#include "pair_acquisition.hpp"
#include "pma_endpoint.hpp"

// Pair acquisition corrects the received wiring and skew before the existing
// PCS is allowed to qualify its scrambler and advertise receiver readiness.
class AlignedPMAEndpoint : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) master_in;
    _PORT(bool) tick_ms_in;
    _PORT(bool) samples_valid_in;
    _PORT(bool) receiver_quality_in;
    _PORT(u<32>) samples_in;
    _PORT(u<33>) seed_in;
    _PORT(u<8>) transmit_data_in;
    _PORT(bool) transmit_enable_in;
    _PORT(bool) transmit_error_in;
    _PORT(u<12>) symbols_out = _ASSIGN(endpoint.symbols_out());
    _PORT(u<10>) receive_out = _ASSIGN(endpoint.receive_out());
    _PORT(bool) link_out = _ASSIGN(endpoint.link_out());
    _PORT(bool) timeout_out = _ASSIGN(endpoint.timeout_out());
    _PORT(bool) locked_out = _ASSIGN(endpoint.locked_out());
    _PORT(u<2>) mode_out = _ASSIGN(endpoint.mode_out());
    _PORT(bool) pairs_locked_out = _ASSIGN(pairs.locked_out());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(pairs.mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(pairs.delays_out());
private:
    ReceivePairAcquirer pairs;
    PMAEndpoint endpoint;
public:
    void _assign() {
        pairs.samples_in = _ASSIGN(samples_in());
        pairs.enable_in = _ASSIGN(enable_in());
        pairs.restart_in = _ASSIGN(restart_in());
        pairs.signal_ok_in = _ASSIGN(receiver_quality_in() && samples_valid_in());
        pairs.advance_in = _ASSIGN(samples_valid_in());
        pairs.remote_master_in = _ASSIGN(!master_in());
        endpoint.enable_in = _ASSIGN(enable_in());
        endpoint.restart_in = _ASSIGN(restart_in());
        endpoint.master_in = _ASSIGN(master_in());
        endpoint.tick_ms_in = _ASSIGN(tick_ms_in());
        endpoint.samples_valid_in = _ASSIGN(pairs.valid_out());
        endpoint.receiver_quality_in = _ASSIGN(receiver_quality_in() && pairs.locked_out());
        endpoint.samples_in = _ASSIGN(pairs.samples_out());
        endpoint.seed_in = _ASSIGN(seed_in());
        endpoint.transmit_data_in = _ASSIGN(transmit_data_in());
        endpoint.transmit_enable_in = _ASSIGN(transmit_enable_in());
        endpoint.transmit_error_in = _ASSIGN(transmit_error_in());
        pairs._assign();
        endpoint._assign();
    }
    void _work(bool reset) {
        pairs._work(reset);
        endpoint._work(reset);
    }
    void _strobe() {
        pairs._strobe();
        endpoint._strobe();
    }
};
