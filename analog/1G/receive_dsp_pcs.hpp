#pragma once
#include "receive_dsp.hpp"
#include "pair_acquisition.hpp"
#include "pcs_receiver.hpp"
#include "receive_quality.hpp"

// Recovered physical-lane symbols -> adaptive cancellation -> cable pair
// identification/deskew -> receive PCS. A missing symbol tick holds acquisition;
// a lost signal, restart or transport fault revokes receiver availability.
class ReceiveDSPPCS : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) clipped_in;
    _PORT(bool) train_in;
    _PORT(bool) valid_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) signal_ready_in;
    _PORT(bool) transport_fault_in;
    _PORT(bool) remote_master_in;
    _PORT(u<10>) receive_out = _ASSIGN(pcs.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(pcs.receive_valid_out() && receiver_available_comb_func());
    _PORT(bool) locked_out = _ASSIGN(pcs.locked_out() && receiver_available_comb_func());
    _PORT(bool) quality_out = _ASSIGN(quality.ready_out() && receiver_available_comb_func());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(pcs.remote_receiver_ok_out() && receiver_available_comb_func());
    _PORT(bool) fault_out = _ASSIGN(transport_fault_in() || dsp.fault_out());
    _PORT(bool) pairs_locked_out = _ASSIGN(pairs.locked_out() && acquisition_available_comb_func());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(pairs.mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(pairs.delays_out());
    _PORT(bool) training_busy_out = _ASSIGN(dsp.training_busy_out());

private:
    ReceiveDSP dsp;
    ReceivePairAcquirer pairs;
    PCSReceiver pcs;
    ReceiveQuality quality;

    _LAZY_COMB(frontend_available_comb, bool)
        return frontend_available_comb = enable_in() && !restart_in() && signal_ready_in() &&
               !transport_fault_in() && !dsp.fault_out();
    }
    _LAZY_COMB(acquisition_available_comb, bool)
        // Do not latch cable identity from a filter whose training updates are
        // still changing its output. Bubbles after training do not revoke it.
        return acquisition_available_comb = frontend_available_comb_func() && !train_in() && !dsp.training_busy_out();
    }
    _LAZY_COMB(receiver_available_comb, bool)
        return receiver_available_comb = acquisition_available_comb_func() && pairs.locked_out();
    }

public:
    void _assign() {
        dsp.samples_in = _ASSIGN(samples_in());
        dsp.local_in = _ASSIGN(local_in());
        dsp.desired_in = _ASSIGN(desired_in());
        dsp.tag_in = _ASSIGN(tag_in());
        dsp.clipped_in = _ASSIGN(clipped_in());
        dsp.train_in = _ASSIGN(train_in());
        dsp.valid_in = _ASSIGN(valid_in() && enable_in() && signal_ready_in() && !transport_fault_in());
        dsp.restart_in = _ASSIGN(restart_in() || !enable_in() || !signal_ready_in() || transport_fault_in());
        pairs.samples_in = _ASSIGN(dsp.samples_out());
        pairs.enable_in = _ASSIGN(enable_in());
        pairs.restart_in = _ASSIGN(restart_in());
        pairs.signal_ok_in = _ASSIGN(acquisition_available_comb_func());
        pairs.advance_in = _ASSIGN(dsp.valid_out());
        pairs.remote_master_in = _ASSIGN(remote_master_in());
        pcs.samples_in = _ASSIGN(pairs.samples_out());
        pcs.symbol_enable_in = _ASSIGN(pairs.valid_out() && receiver_available_comb_func());
        pcs.remote_master_in = _ASSIGN(remote_master_in());
        pcs.pma_ok_in = _ASSIGN(receiver_available_comb_func());
        quality.receive_in = _ASSIGN(pcs.receive_out());
        quality.valid_in = _ASSIGN(pcs.receive_valid_out());
        quality.locked_in = _ASSIGN(pcs.locked_out());
        quality.fault_in = _ASSIGN(transport_fault_in() || dsp.fault_out());
        quality.clipped_in = _ASSIGN(dsp.valid_out() && dsp.clipped_out());
        quality.enable_in = _ASSIGN(receiver_available_comb_func() && !train_in() && !dsp.training_busy_out());
        dsp._assign();
        pairs._assign();
        pcs._assign();
        quality._assign();
    }
    void _work(bool reset) {
        dsp._work(reset || restart_in() || !enable_in() || transport_fault_in());
        pairs._work(reset || restart_in() || !enable_in() || transport_fault_in());
        pcs._work(reset || !receiver_available_comb_func());
        quality._work(reset || restart_in() || !enable_in() || transport_fault_in());
    }
    void _strobe() {
        dsp._strobe();
        pairs._strobe();
        pcs._strobe();
        quality._strobe();
    }
};
