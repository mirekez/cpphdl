#pragma once
#include "autonomous_adc_receive_phy.hpp"
#include "phy_reset_control.hpp"

// Connect clock qualification to the real SAR, recovery, FIFO and PCS resets.
// Reset assertion is asynchronous; each delivered domain releases locally.
// Exchange release acknowledgements before either side admits new traffic.
class QualifiedADCReceivePHY : public Module {
public:
    _PORT(u<16>) comparisons_in[4];
    _PORT(u<8>) offset_in[4];
    _PORT(u<16>) gain_in[4];
    _PORT(u<16>) time_in;
    _PORT(u<12>) local_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<8>) trial_out[64];
    _PORT(bool) acquire_out[64];
    _PORT(bool) convert_out[64];
    _PORT(u<16>) aperture_out;
    _PORT(bool) startup_ready_out;
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
    _PORT(u<10>) receive_out;
    _PORT(bool) receive_valid_out;
    _PORT(bool) locked_out;
    _PORT(bool) quality_out;
    _PORT(bool) pairs_locked_out;
    _PORT(bool) remote_receiver_ok_out;
    _PORT(u<8>) pair_mapping_out;
    _PORT(u<20>) pair_delays_out;
    _PORT(bool) acquiring_out;
    _PORT(bool) fault_out;
    _PORT(u<25>) period_out;
    _PORT(u<16>) recovered_tag_out;
    _PORT(bool) recovered_valid_out;
    _PORT(bool) power_good_in;
    _PORT(bool) reset_adc_out;
    _PORT(bool) reset_pcs_out;

private:
    PHYResetControl reset_control;
    AutonomousADCReceivePHY receiver;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> pcs_release_adc1{}, pcs_release_adc2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> adc_release_pcs1{}, adc_release_pcs2{};

    _LAZY_COMB(adc_hold_comb, bool)
        return adc_hold_comb = reset_control.reset_adc_out() || !pcs_release_adc2;
    }
    _LAZY_COMB(pcs_hold_comb, bool)
        return pcs_hold_comb = reset_control.reset_pcs_out() || !adc_release_pcs2;
    }

public:
    void _assign() {
        reset_control.enable_in = _ASSIGN(enable_in() && !restart_in());
        reset_control.power_good_in = _ASSIGN(power_good_in());
        reset_control.adc_ready_in = _ASSIGN(clock_ready_in());
        reset_control.pcs_ready_in = _ASSIGN(processing_ready_in());
        receiver.clear_adc_in = _ASSIGN(adc_hold_comb_func());
        receiver.clear_pcs_in = _ASSIGN(pcs_hold_comb_func());
        reset_adc_out = _ASSIGN(adc_hold_comb_func());
        reset_pcs_out = _ASSIGN(pcs_hold_comb_func());
        receiver.time_in = _ASSIGN(time_in());
        receiver.local_in = _ASSIGN(local_in());
        receiver.sample_enable_in = _ASSIGN(sample_enable_in());
        aperture_out = _ASSIGN(adc_hold_comb_func() ? u<16>(0) : receiver.aperture_out());
        startup_ready_out = _ASSIGN(!adc_hold_comb_func() && receiver.startup_ready_out());
        receiver.track_in = _ASSIGN(track_in());
        receiver.initial_phase_in = _ASSIGN(initial_phase_in());
        receiver.origin_in = _ASSIGN(origin_in());
        receiver.enable_in = _ASSIGN(enable_in());
        receiver.restart_in = _ASSIGN(restart_in());
        receiver.clock_ready_in = _ASSIGN(clock_ready_in());
        receiver.processing_ready_in = _ASSIGN(processing_ready_in());
        receiver.signal_ready_in = _ASSIGN(signal_ready_in());
        receiver.transport_fault_in = _ASSIGN(transport_fault_in());
        receiver.remote_master_in = _ASSIGN(remote_master_in());
        receive_out = _ASSIGN(receiver.receive_out());
        receive_valid_out = _ASSIGN(receiver.receive_valid_out());
        locked_out = _ASSIGN(receiver.locked_out());
        quality_out = _ASSIGN(receiver.quality_out());
        pairs_locked_out = _ASSIGN(receiver.pairs_locked_out());
        remote_receiver_ok_out = _ASSIGN(receiver.remote_receiver_ok_out());
        pair_mapping_out = _ASSIGN(receiver.pair_mapping_out());
        pair_delays_out = _ASSIGN(receiver.pair_delays_out());
        acquiring_out = _ASSIGN(receiver.acquiring_out());
        fault_out = _ASSIGN(receiver.fault_out());
        period_out = _ASSIGN(receiver.period_out());
        recovered_tag_out = _ASSIGN(receiver.recovered_tag_out());
        recovered_valid_out = _ASSIGN(receiver.recovered_valid_out());
        for (unsigned i = 0; i < 4; ++i) {
            receiver.comparisons_in[i] = _ASSIGN_I(comparisons_in[i]());
            receiver.offset_in[i] = _ASSIGN_I(offset_in[i]());
            receiver.gain_in[i] = _ASSIGN_I(gain_in[i]());
        }
        for (unsigned i = 0; i < 64; ++i) {
            trial_out[i] = _ASSIGN_I(receiver.trial_out[i]());
            acquire_out[i] = _ASSIGN_I(!adc_hold_comb_func() && receiver.acquire_out[i]());
            convert_out[i] = _ASSIGN_I(!adc_hold_comb_func() && receiver.convert_out[i]());
        }
        reset_control._assign();
        receiver._assign();
    }
    void _work_ref_clk(bool reset) { reset_control._work_ref_clk(reset); }
    void _strobe_ref_clk() { reset_control._strobe_ref_clk(); }
    void _work_adc_clk(bool reset) {
        reset_control._work_adc_clk(reset);
        pcs_release_adc1._next = !reset_control.reset_pcs_out();
        pcs_release_adc2._next = pcs_release_adc1;
        if (reset || reset_control.reset_adc_out()) {
            pcs_release_adc1._next = 0;
            pcs_release_adc2._next = 0;
        }
        receiver._work_adc_clk(reset);
    }
    void _strobe_adc_clk() {
        reset_control._strobe_adc_clk();
        pcs_release_adc1.strobe();
        pcs_release_adc2.strobe();
        receiver._strobe_adc_clk();
    }
    void _work_pcs_clk(bool reset) {
        reset_control._work_pcs_clk(reset);
        adc_release_pcs1._next = !reset_control.reset_adc_out();
        adc_release_pcs2._next = adc_release_pcs1;
        if (reset || reset_control.reset_pcs_out()) {
            adc_release_pcs1._next = 0;
            adc_release_pcs2._next = 0;
        }
        receiver._work_pcs_clk(reset);
    }
    void _strobe_pcs_clk() {
        reset_control._strobe_pcs_clk();
        adc_release_pcs1.strobe();
        adc_release_pcs2.strobe();
        receiver._strobe_pcs_clk();
    }
    void _work_neg_ref_clk(bool) {}
    void _work_neg_adc_clk(bool) {}
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_ref_clk() {}
    void _strobe_neg_adc_clk() {}
    void _strobe_neg_pcs_clk() {}
#ifndef SYNTHESIS
    // Event dispatch for the native simulator. Commit using the normal strobes.
    void sim_assert_ref_reset() { reset_control.sim_assert_ref_reset(); }
    void sim_assert_adc_reset() { reset_control.sim_assert_adc_reset(); }
    void sim_assert_pcs_reset() { reset_control.sim_assert_pcs_reset(); }
#endif
};
