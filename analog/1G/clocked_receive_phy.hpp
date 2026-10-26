#pragma once
#include "adc_receive_phy.hpp"
#include "reference_clock_monitor.hpp"

// The external reference and the physical PLL output are separate clock ports.
// Clock health starts/stops the actual four-lane SAR-to-PCS receive pipeline.
class ClockedReceivePHY : public Module {
public:
    _PORT(u<16>) comparisons_in[4];
    _PORT(u<8>) offset_in[4];
    _PORT(u<16>) gain_in[4];
    _PORT(u<16>) time_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(bool) train_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<8>) trial_out[64];
    _PORT(bool) acquire_out[64];
    _PORT(bool) convert_out[64];
    _PORT(u<16>) aperture_out = _ASSIGN(receiver.aperture_out());
    _PORT(bool) startup_ready_out = _ASSIGN(receiver.startup_ready_out());
    _PORT(bool) track_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) power_good_in;
    _PORT(bool) clock_ready_out = _ASSIGN(monitor.ref_ready_out() && monitor.adc_ready_out());
    _PORT(bool) signal_ready_in;
    _PORT(bool) transport_fault_in;
    _PORT(bool) remote_master_in;
    _PORT(u<10>) receive_out = _ASSIGN(receiver.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(receiver.receive_valid_out());
    _PORT(bool) locked_out = _ASSIGN(receiver.locked_out());
    _PORT(bool) quality_out = _ASSIGN(receiver.quality_out());
    _PORT(bool) pairs_locked_out = _ASSIGN(receiver.pairs_locked_out());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(receiver.remote_receiver_ok_out());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(receiver.pair_mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(receiver.pair_delays_out());
    _PORT(bool) training_busy_out = _ASSIGN(receiver.training_busy_out());
    _PORT(bool) fault_out = _ASSIGN(receiver.fault_out());
    _PORT(u<25>) period_out = _ASSIGN(receiver.period_out());
    _PORT(u<16>) recovered_tag_out = _ASSIGN(receiver.recovered_tag_out());
    _PORT(bool) recovered_valid_out = _ASSIGN(receiver.recovered_valid_out());

private:
    ReferenceClockMonitor monitor;
    ADCReceivePHY receiver;

public:
    void _assign() {
        monitor.power_good_in = _ASSIGN(power_good_in());
        receiver.clock_ready_in = _ASSIGN(monitor.ref_ready_out() && monitor.adc_ready_out());
        receiver.time_in = _ASSIGN(time_in());
        receiver.local_in = _ASSIGN(local_in());
        receiver.desired_in = _ASSIGN(desired_in());
        receiver.train_in = _ASSIGN(train_in());
        receiver.sample_enable_in = _ASSIGN(sample_enable_in());
        receiver.track_in = _ASSIGN(track_in());
        receiver.initial_phase_in = _ASSIGN(initial_phase_in());
        receiver.origin_in = _ASSIGN(origin_in());
        receiver.enable_in = _ASSIGN(enable_in());
        receiver.restart_in = _ASSIGN(restart_in());
        receiver.signal_ready_in = _ASSIGN(signal_ready_in());
        receiver.transport_fault_in = _ASSIGN(transport_fault_in());
        receiver.remote_master_in = _ASSIGN(remote_master_in());
        for (unsigned i = 0; i < 4; ++i) {
            receiver.comparisons_in[i] = _ASSIGN_I(comparisons_in[i]());
            receiver.offset_in[i] = _ASSIGN_I(offset_in[i]());
            receiver.gain_in[i] = _ASSIGN_I(gain_in[i]());
        }
        for (unsigned i = 0; i < 64; ++i) {
            trial_out[i] = _ASSIGN_I(receiver.trial_out[i]());
            acquire_out[i] = _ASSIGN_I(receiver.acquire_out[i]());
            convert_out[i] = _ASSIGN_I(receiver.convert_out[i]());
        }
        monitor._assign();
        receiver._assign();
    }
    void _work_adc_clk(bool reset) {
        monitor._work_adc_clk(reset);
        receiver._work(reset);
    }
    void _strobe_adc_clk() {
        monitor._strobe_adc_clk();
        receiver._strobe();
    }
    void _work_ref_clk(bool reset) { monitor._work_ref_clk(reset); }
    void _strobe_ref_clk() { monitor._strobe_ref_clk(); }
    void _work_neg_adc_clk(bool) {}
    void _strobe_neg_adc_clk() {}
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
};
