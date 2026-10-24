#pragma once
#include "pcs_receiver.hpp"
#include "receive_sample_recovery.hpp"

// One clock domain: recovered symbols are clock enables, never generated clocks.
class ReceiveSamplePCS : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) valid_in;
    _PORT(bool) restart_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(bool) remote_master_in;
    _PORT(bool) pma_ok_in;
    _PORT(u<10>) receive_out = _ASSIGN(pcs.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(pcs.receive_valid_out());
    _PORT(bool) locked_out = _ASSIGN(pcs.locked_out());
    _PORT(bool) fault_out = _ASSIGN(recovery.fault_out());
    _PORT(bool) symbol_valid_out = _ASSIGN(recovery.valid_out());
    _PORT(u<16>) symbol_tag_out = _ASSIGN(recovery.tag_out());
    _PORT(u<24>) symbol_fraction_out = _ASSIGN(recovery.fraction_out());

private:
    ReceiveSampleRecovery recovery;
    PCSReceiver pcs;

public:
    void _assign() {
        recovery.samples_in = _ASSIGN(samples_in());
        recovery.tag_in = _ASSIGN(tag_in());
        recovery.valid_in = _ASSIGN(valid_in());
        recovery.track_in = _ASSIGN(true);
        recovery.restart_in = _ASSIGN(restart_in());
        recovery.initial_phase_in = _ASSIGN(initial_phase_in());
        recovery.origin_in = _ASSIGN(origin_in());
        pcs.samples_in = _ASSIGN(recovery.samples_out());
        pcs.symbol_enable_in = _ASSIGN(recovery.valid_out() && !recovery.fault_out());
        pcs.remote_master_in = _ASSIGN(remote_master_in());
        pcs.pma_ok_in = _ASSIGN(pma_ok_in() && !recovery.fault_out());
        recovery._assign();
        pcs._assign();
    }
    void _work(bool reset) {
        recovery._work(reset);
        pcs._work(reset || restart_in());
    }
    void _strobe() {
        recovery._strobe();
        pcs._strobe();
    }
};
