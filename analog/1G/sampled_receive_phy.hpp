#pragma once
#include "receive_sample_recovery.hpp"
#include "receive_sample_context.hpp"
#include "receive_dsp_pcs.hpp"

// The digital receive path from tagged ADC samples through recovered symbols
// to decoded Ethernet words. All children use the reference-derived ADC clock;
// recovered symbols advance the DSP/PCS by enables, not by a generated clock.
class SampledReceivePHY : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) sample_tag_in;
    _PORT(bool) sample_valid_in;
    _PORT(bool) clipped_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(bool) train_in;
    _PORT(bool) track_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) clock_ready_in;
    _PORT(bool) signal_ready_in;
    _PORT(bool) transport_fault_in;
    _PORT(bool) remote_master_in;
    _PORT(u<10>) receive_out = _ASSIGN(receiver.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(receiver.receive_valid_out() && available_comb_func());
    _PORT(bool) locked_out = _ASSIGN(receiver.locked_out() && available_comb_func());
    _PORT(bool) quality_out = _ASSIGN(receiver.quality_out() && available_comb_func());
    _PORT(bool) pairs_locked_out = _ASSIGN(receiver.pairs_locked_out() && available_comb_func());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(receiver.remote_receiver_ok_out() && available_comb_func());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(receiver.pair_mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(receiver.pair_delays_out());
    _PORT(bool) training_busy_out = _ASSIGN(receiver.training_busy_out() || metadata.train_out());
    _PORT(bool) fault_out = _ASSIGN(transport_fault_in() || recovery.fault_out() || metadata.fault_out() || receiver.fault_out());
    _PORT(u<25>) period_out = _ASSIGN(recovery.period_out());
    _PORT(u<16>) recovered_tag_out = _ASSIGN(recovery.tag_out());
    _PORT(bool) recovered_valid_out = _ASSIGN(recovery.valid_out() && available_comb_func());

private:
    ReceiveSampleRecovery recovery;
    ReceiveSampleContext metadata;
    ReceiveDSPPCS receiver;

    _LAZY_COMB(available_comb, bool)
        return available_comb = enable_in() && !restart_in() && clock_ready_in() && signal_ready_in() &&
               !transport_fault_in() && !recovery.fault_out() && !metadata.fault_out();
    }
    _LAZY_COMB(clear_comb, bool)
        return clear_comb = restart_in() || !enable_in() || !clock_ready_in() || !signal_ready_in();
    }

public:
    void _assign() {
        recovery.samples_in = _ASSIGN(samples_in());
        recovery.tag_in = _ASSIGN(sample_tag_in());
        recovery.valid_in = _ASSIGN(sample_valid_in() && !transport_fault_in());
        recovery.track_in = _ASSIGN(track_in());
        recovery.restart_in = _ASSIGN(clear_comb_func());
        recovery.initial_phase_in = _ASSIGN(initial_phase_in());
        recovery.origin_in = _ASSIGN(origin_in());
        metadata.sample_tag_in = _ASSIGN(sample_tag_in());
        metadata.local_in = _ASSIGN(local_in());
        metadata.desired_in = _ASSIGN(desired_in());
        metadata.train_in = _ASSIGN(train_in());
        metadata.clipped_in = _ASSIGN(clipped_in());
        metadata.sample_valid_in = _ASSIGN(sample_valid_in() && !transport_fault_in());
        metadata.center_tag_in = _ASSIGN(recovery.tag_out());
        metadata.center_fraction_in = _ASSIGN(recovery.fraction_out());
        metadata.center_valid_in = _ASSIGN(recovery.valid_out() && !recovery.fault_out());
        receiver.samples_in = _ASSIGN(recovery.samples_out());
        receiver.local_in = _ASSIGN(metadata.local_out());
        receiver.desired_in = _ASSIGN(metadata.desired_out());
        receiver.tag_in = _ASSIGN(recovery.tag_out());
        receiver.clipped_in = _ASSIGN(recovery.clipped_out() || metadata.clipped_out());
        receiver.train_in = _ASSIGN(metadata.train_out());
        receiver.valid_in = _ASSIGN(recovery.valid_out() && metadata.valid_out() && available_comb_func());
        receiver.enable_in = _ASSIGN(enable_in());
        receiver.restart_in = _ASSIGN(clear_comb_func());
        receiver.signal_ready_in = _ASSIGN(available_comb_func());
        receiver.transport_fault_in = _ASSIGN(transport_fault_in() || recovery.fault_out() || metadata.fault_out());
        receiver.remote_master_in = _ASSIGN(remote_master_in());
        recovery._assign();
        metadata._assign();
        receiver._assign();
    }
    void _work(bool reset) {
        recovery._work(reset || clear_comb_func());
        metadata._work(reset || clear_comb_func());
        receiver._work(reset || clear_comb_func());
    }
    void _strobe() {
        recovery._strobe();
        metadata._strobe();
        receiver._strobe();
    }
};
