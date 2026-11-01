#pragma once
#include "receive_sample_recovery.hpp"
#include "receive_sample_context.hpp"
#include "receive_dsp_pcs.hpp"
#include "receive_dsp_crossing.hpp"

// Keep interpolation in the 250 MHz ADC domain and transfer complete recovered
// observations into the independently qualified 132..144 MHz processing domain.
// Both domains must observe reset before either readiness input admits traffic.
class AsynchronousReceivePHY : public Module {
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
    _PORT(bool) processing_ready_in;
    _PORT(bool) signal_ready_in;
    _PORT(bool) transport_fault_in;
    _PORT(bool) remote_master_in;
    _PORT(u<10>) receive_out = _ASSIGN(receiver.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(receiver.receive_valid_out() && delivered_comb_func());
    _PORT(bool) locked_out = _ASSIGN(receiver.locked_out() && delivered_comb_func());
    _PORT(bool) quality_out = _ASSIGN(receiver.quality_out() && delivered_comb_func());
    _PORT(bool) pairs_locked_out = _ASSIGN(receiver.pairs_locked_out() && delivered_comb_func());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(receiver.remote_receiver_ok_out() && delivered_comb_func());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(receiver.pair_mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(receiver.pair_delays_out());
    _PORT(bool) training_busy_out = _ASSIGN(receiver.training_busy_out() || crossing.train_out());
    _PORT(bool) fault_out = _ASSIGN(transport_fault_in() || crossing.source_fault_out() || crossing.fault_out() || recovery.fault_out() || metadata.fault_out() || receiver.fault_out());
    _PORT(u<25>) period_out = _ASSIGN(recovery.period_out());
    _PORT(u<16>) recovered_tag_out = _ASSIGN(recovery.tag_out());
    _PORT(bool) recovered_valid_out = _ASSIGN(recovery.valid_out() && available_comb_func());

private:
    ReceiveSampleRecovery recovery;
    ReceiveSampleContext metadata;
    ReceiveDSPCrossing crossing;
    ReceiveDSPPCS receiver;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> available_sync1{}, available_sync2{};

    _LAZY_COMB(delivered_comb, bool)
        return delivered_comb = available_sync2 && enable_in() && !restart_in() && processing_ready_in() &&
                                clock_ready_in() && signal_ready_in() && !transport_fault_in() && !crossing.fault_out();
    }

    _LAZY_COMB(available_comb, bool)
        return available_comb = enable_in() && !restart_in() && clock_ready_in() && processing_ready_in() && signal_ready_in() &&
               !transport_fault_in() && !recovery.fault_out() && !metadata.fault_out();
    }
    _LAZY_COMB(clear_comb, bool)
        return clear_comb = restart_in() || !enable_in() || !clock_ready_in() || !processing_ready_in() || !signal_ready_in();
    }

public:
    void _assign() {
        crossing.clear_adc_in = _ASSIGN(false);
        crossing.clear_pcs_in = _ASSIGN(false);
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
        crossing.samples_in = _ASSIGN(recovery.samples_out());
        crossing.tag_in = _ASSIGN(recovery.tag_out());
        crossing.local_in = _ASSIGN(metadata.local_out());
        crossing.desired_in = _ASSIGN(metadata.desired_out());
        crossing.clipped_in = _ASSIGN(recovery.clipped_out() || metadata.clipped_out());
        crossing.train_in = _ASSIGN(metadata.train_out());
        crossing.valid_in = _ASSIGN(recovery.valid_out() && metadata.valid_out() && available_comb_func());
        crossing.fault_in = _ASSIGN(transport_fault_in() || recovery.fault_out() || metadata.fault_out());
        receiver.samples_in = _ASSIGN(crossing.samples_out());
        receiver.tag_in = _ASSIGN(crossing.tag_out());
        receiver.local_in = _ASSIGN(crossing.local_out());
        receiver.desired_in = _ASSIGN(crossing.desired_out());
        receiver.clipped_in = _ASSIGN(crossing.clipped_out());
        receiver.train_in = _ASSIGN(crossing.train_out());
        receiver.valid_in = _ASSIGN(crossing.valid_out() && delivered_comb_func());
        receiver.enable_in = _ASSIGN(enable_in());
        receiver.restart_in = _ASSIGN(clear_comb_func());
        receiver.signal_ready_in = _ASSIGN(delivered_comb_func());
        receiver.transport_fault_in = _ASSIGN(crossing.fault_out());
        receiver.remote_master_in = _ASSIGN(remote_master_in());
        recovery._assign();
        metadata._assign();
        crossing._assign();
        receiver._assign();
    }
    void _work_adc_clk(bool reset) {
        recovery._work(reset || clear_comb_func());
        metadata._work(reset || clear_comb_func());
        crossing._work_adc_clk(reset || clear_comb_func());
    }
    void _strobe_adc_clk() {
        recovery._strobe();
        metadata._strobe();
        crossing._strobe_adc_clk();
    }
    void _work_pcs_clk(bool reset) {
        available_sync1._next = available_comb_func();
        available_sync2._next = available_sync1;
        crossing._work_pcs_clk(reset || clear_comb_func());
        receiver._work(reset || clear_comb_func());
        if (reset || clear_comb_func()) {
            available_sync1._next = 0;
            available_sync2._next = 0;
        }
    }
    void _strobe_pcs_clk() {
        crossing._strobe_pcs_clk();
        receiver._strobe();
        available_sync1.strobe();
        available_sync2.strobe();
    }
    void _work_neg_adc_clk(bool) {}
    void _strobe_neg_adc_clk() {}
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_pcs_clk() {}
};
