#pragma once
#include "adc_receive_capture.hpp"
#include "autonomous_receive_phy.hpp"

// Comparator decisions and actual sampling controls form the front boundary.
// SAR sequencing and timing recovery use the 250 MHz ADC clock.
// Autonomous DSP/PCS runs on its separately qualified processing clock.
class AutonomousADCReceivePHY : public Module {
public:
    _PORT(bool) clear_adc_in;
    _PORT(bool) clear_pcs_in;
    _PORT(u<16>) comparisons_in[4];
    _PORT(u<8>) offset_in[4];
    _PORT(u<16>) gain_in[4];
    _PORT(u<16>) time_in;
    _PORT(u<12>) local_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<8>) trial_out[64];
    _PORT(bool) acquire_out[64];
    _PORT(bool) convert_out[64];
    _PORT(u<16>) aperture_out = _ASSIGN(capture.aperture_out());
    _PORT(bool) startup_ready_out = _ASSIGN(capture.startup_ready_out());
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
    _PORT(bool) receive_valid_out = _ASSIGN(receiver.receive_valid_out());
    _PORT(bool) locked_out = _ASSIGN(receiver.locked_out());
    _PORT(bool) quality_out = _ASSIGN(receiver.quality_out());
    _PORT(bool) pairs_locked_out = _ASSIGN(receiver.pairs_locked_out());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(receiver.remote_receiver_ok_out());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(receiver.pair_mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(receiver.pair_delays_out());
    _PORT(bool) acquiring_out = _ASSIGN(receiver.acquiring_out());
    _PORT(bool) fault_out = _ASSIGN(capture.fault_out() || receiver.fault_out());
    _PORT(u<25>) period_out = _ASSIGN(receiver.period_out());
    _PORT(u<16>) recovered_tag_out = _ASSIGN(receiver.recovered_tag_out());
    _PORT(bool) recovered_valid_out = _ASSIGN(receiver.recovered_valid_out());

private:
    ADCReceiveCapture capture;
    AutonomousReceivePHY receiver;

public:
    void _assign() {
        capture.enable_in = _ASSIGN(enable_in() && clock_ready_in() && !restart_in() && !clear_adc_in());
        receiver.clear_adc_in = _ASSIGN(clear_adc_in());
        receiver.clear_pcs_in = _ASSIGN(clear_pcs_in());
        capture.sample_enable_in = _ASSIGN(sample_enable_in());
        capture.time_in = _ASSIGN(time_in());
        capture.local_in = _ASSIGN(local_in());
        capture.desired_in = _ASSIGN(1170);
        capture.train_in = _ASSIGN(false);
        for (unsigned i = 0; i < 4; ++i) {
            capture.comparisons_in[i] = _ASSIGN_I(comparisons_in[i]());
            capture.offset_in[i] = _ASSIGN_I(offset_in[i]());
            capture.gain_in[i] = _ASSIGN_I(gain_in[i]());
        }
        for (unsigned i = 0; i < 64; ++i) {
            trial_out[i] = _ASSIGN_I(capture.trial_out[i]());
            acquire_out[i] = _ASSIGN_I(capture.acquire_out[i]());
            convert_out[i] = _ASSIGN_I(capture.convert_out[i]());
        }
        receiver.samples_in = _ASSIGN(capture.samples_out());
        receiver.sample_tag_in = _ASSIGN(capture.tag_out());
        receiver.sample_valid_in = _ASSIGN(capture.valid_out());
        receiver.local_in = _ASSIGN(capture.local_out());
        receiver.clipped_in = _ASSIGN(capture.clipped_out());
        receiver.transport_fault_in = _ASSIGN(transport_fault_in() || capture.fault_out());
        receiver.track_in = _ASSIGN(track_in());
        receiver.initial_phase_in = _ASSIGN(initial_phase_in());
        receiver.origin_in = _ASSIGN(origin_in());
        receiver.enable_in = _ASSIGN(enable_in());
        receiver.restart_in = _ASSIGN(restart_in());
        receiver.clock_ready_in = _ASSIGN(clock_ready_in());
        receiver.processing_ready_in = _ASSIGN(processing_ready_in());
        receiver.signal_ready_in = _ASSIGN(signal_ready_in());
        receiver.remote_master_in = _ASSIGN(remote_master_in());
        capture._assign();
        receiver._assign();
    }
    void _work_adc_clk(bool reset) {
        capture._work(reset || clear_adc_in());
        receiver._work_adc_clk(reset);
    }
    void _strobe_adc_clk() {
        capture._strobe();
        receiver._strobe_adc_clk();
    }
    void _work_pcs_clk(bool reset) {
        receiver._work_pcs_clk(reset);
    }
    void _strobe_pcs_clk() {
        receiver._strobe_pcs_clk();
    }
    void _work_ref_clk(bool) {}
    void _strobe_ref_clk() {}
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
};
