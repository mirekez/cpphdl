#pragma once
#include "repeater_port.hpp"
#include "../../1G/flow_controlled_repeater.hpp"
#include "../../1G/reference_clock_monitor.hpp"
#include "../../1G/processing_clock_control.hpp"

// Complete two-port digital hierarchy at the electrical IC boundary. ADC,
// PCS and each transmitter have explicit delivered-clock domains. The raw
// oscillator clocks its controller even while the physical PCS gate is shut.
class RepeaterDigital : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) power_good_in;
    _PORT(bool) restart_in[2];
    _PORT(bool) signal_ready_in[2];
    _PORT(bool) tx_clock_ready_in[2];
    _PORT(bool) pulse_in[2];
    _PORT(u<11>) configuration_in[2];
    _PORT(u<11>) negotiation_seed_in[2];
    _PORT(u<33>) scrambler_seed_in[2];
    _PORT(u<48>) address_in[2];
    _PORT(u<16>) comparisons_in[8];
    _PORT(u<8>) offset_in[8];
    _PORT(u<16>) gain_in[8];
    _PORT(u<12>) local_history_in[2];

    _PORT(bool) clear_out[8], evaluate_enable_out[8];
    _PORT(u<16>) aperture_out[2];
    _PORT(u<8>) trial_out[128];
    _PORT(bool) acquire_out[128];
    _PORT(bool) convert_out[128];
    _PORT(u<4>) main_p_out[8], main_n_out[8];
    _PORT(u<4>) history_p_out[8], history_n_out[8];
    _PORT(bool) pulse_out[2], pulse_enable_out[2];
    _PORT(bool) master_out[2], link_out[2];
    _PORT(u<12>) symbols_out[2];
    _PORT(u<25>) recovered_period_out[2];
    _PORT(u<16>) recovered_tag_out[2];
    _PORT(bool) recovered_valid_out[2];
    _PORT(u<8>) processing_coarse_n_out, processing_fine_n_out;
    _PORT(bool) processing_clear_out, processing_admit_out;
    _PORT(bool) adc_ready_out, processing_ready_out;
    _PORT(bool) active_out, fault_out;

private:
    RepeaterPort port_a, port_b;
    FlowControlledRepeater forwarding;
    ReferenceClockMonitor adc_clock;
    ProcessingClockControl processing_clock;

public:
    void _assign() {
        adc_clock.power_good_in = _ASSIGN(power_good_in());
        processing_clock.enable_in = _ASSIGN(enable_in() && power_good_in());
        processing_coarse_n_out = _ASSIGN(processing_clock.coarse_n_out());
        processing_fine_n_out = _ASSIGN(processing_clock.fine_n_out());
        processing_clear_out = _ASSIGN(processing_clock.clear_out());
        processing_admit_out = _ASSIGN(processing_clock.ready_pcs_out());
        adc_ready_out = _ASSIGN(adc_clock.ref_ready_out() && adc_clock.adc_ready_out());
        processing_ready_out = _ASSIGN(processing_clock.ready_ref_out() && processing_clock.ready_pcs_out());
        port_a.enable_in = _ASSIGN(enable_in());
        port_a.power_good_in = _ASSIGN(power_good_in());
        port_a.adc_ready_in = _ASSIGN(adc_clock.ref_ready_out() && adc_clock.adc_ready_out());
        port_a.pcs_ready_in = _ASSIGN(processing_clock.ready_ref_out() && processing_clock.ready_pcs_out());
        port_a.tx_ready_in = _ASSIGN(tx_clock_ready_in[0]());
        port_a.signal_ready_in = _ASSIGN(signal_ready_in[0]());
        port_a.pulse_in = _ASSIGN(pulse_in[0]());
        port_a.restart_in = _ASSIGN(restart_in[0]());
        port_a.configuration_in = _ASSIGN(configuration_in[0]());
        port_a.negotiation_seed_in = _ASSIGN(negotiation_seed_in[0]());
        port_a.scrambler_seed_in = _ASSIGN(scrambler_seed_in[0]());
        port_a.local_history_in = _ASSIGN(local_history_in[0]());
        pulse_out[0] = _ASSIGN(port_a.pulse_out());
        pulse_enable_out[0] = _ASSIGN(port_a.pulse_enable_out());
        master_out[0] = _ASSIGN(port_a.master_out());
        link_out[0] = _ASSIGN(port_a.link_out());
        aperture_out[0] = _ASSIGN(port_a.aperture_out());
        symbols_out[0] = _ASSIGN(port_a.symbols_out());
        recovered_period_out[0] = _ASSIGN(port_a.recovered_period_out());
        recovered_tag_out[0] = _ASSIGN(port_a.recovered_tag_out());
        recovered_valid_out[0] = _ASSIGN(port_a.recovered_valid_out());
        // Flatten only the electrical boundary, preserving port/lane/slice
        // identity in the C++ hierarchy. Slots 13..15 are reserved and quiet.
        for (unsigned lane = 0; lane < 4; ++lane) {
            clear_out[lane] = _ASSIGN_INDEXED((lane), port_a.clear_out[lane]());
            evaluate_enable_out[lane] = _ASSIGN_INDEXED((lane), port_a.evaluate_enable_out[lane]());
            port_a.comparisons_in[lane] = _ASSIGN_INDEXED((lane), comparisons_in[lane]());
            port_a.offset_in[lane] = _ASSIGN_INDEXED((lane), offset_in[lane]());
            port_a.gain_in[lane] = _ASSIGN_INDEXED((lane), gain_in[lane]());
            main_p_out[lane] = _ASSIGN_INDEXED((lane), port_a.main_p_out[lane]());
            main_n_out[lane] = _ASSIGN_INDEXED((lane), port_a.main_n_out[lane]());
            history_p_out[lane] = _ASSIGN_INDEXED((lane), port_a.history_p_out[lane]());
            history_n_out[lane] = _ASSIGN_INDEXED((lane), port_a.history_n_out[lane]());
        }
        for (unsigned slice = 0; slice < 64; ++slice) {
            trial_out[slice] = _ASSIGN_INDEXED((slice), port_a.trial_out[slice]());
            acquire_out[slice] = _ASSIGN_INDEXED((slice), port_a.acquire_out[slice]());
            convert_out[slice] = _ASSIGN_INDEXED((slice), port_a.convert_out[slice]());
        }
        port_a._assign();
        port_b.enable_in = _ASSIGN(enable_in());
        port_b.power_good_in = _ASSIGN(power_good_in());
        port_b.adc_ready_in = _ASSIGN(adc_clock.ref_ready_out() && adc_clock.adc_ready_out());
        port_b.pcs_ready_in = _ASSIGN(processing_clock.ready_ref_out() && processing_clock.ready_pcs_out());
        port_b.tx_ready_in = _ASSIGN(tx_clock_ready_in[1]());
        port_b.signal_ready_in = _ASSIGN(signal_ready_in[1]());
        port_b.pulse_in = _ASSIGN(pulse_in[1]());
        port_b.restart_in = _ASSIGN(restart_in[1]());
        port_b.configuration_in = _ASSIGN(configuration_in[1]());
        port_b.negotiation_seed_in = _ASSIGN(negotiation_seed_in[1]());
        port_b.scrambler_seed_in = _ASSIGN(scrambler_seed_in[1]());
        port_b.local_history_in = _ASSIGN(local_history_in[1]());
        pulse_out[1] = _ASSIGN(port_b.pulse_out());
        pulse_enable_out[1] = _ASSIGN(port_b.pulse_enable_out());
        master_out[1] = _ASSIGN(port_b.master_out());
        link_out[1] = _ASSIGN(port_b.link_out());
        aperture_out[1] = _ASSIGN(port_b.aperture_out());
        symbols_out[1] = _ASSIGN(port_b.symbols_out());
        recovered_period_out[1] = _ASSIGN(port_b.recovered_period_out());
        recovered_tag_out[1] = _ASSIGN(port_b.recovered_tag_out());
        recovered_valid_out[1] = _ASSIGN(port_b.recovered_valid_out());
        // Flatten only the electrical boundary, preserving port/lane/slice
        // identity in the C++ hierarchy. Slots 13..15 are reserved and quiet.
        for (unsigned lane = 0; lane < 4; ++lane) {
            clear_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.clear_out[lane]());
            evaluate_enable_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.evaluate_enable_out[lane]());
            port_b.comparisons_in[lane] = _ASSIGN_INDEXED((lane), comparisons_in[4 + lane]());
            port_b.offset_in[lane] = _ASSIGN_INDEXED((lane), offset_in[4 + lane]());
            port_b.gain_in[lane] = _ASSIGN_INDEXED((lane), gain_in[4 + lane]());
            main_p_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.main_p_out[lane]());
            main_n_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.main_n_out[lane]());
            history_p_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.history_p_out[lane]());
            history_n_out[4 + lane] = _ASSIGN_INDEXED((lane), port_b.history_n_out[lane]());
        }
        for (unsigned slice = 0; slice < 64; ++slice) {
            trial_out[64 + slice] = _ASSIGN_INDEXED((slice), port_b.trial_out[slice]());
            acquire_out[64 + slice] = _ASSIGN_INDEXED((slice), port_b.acquire_out[slice]());
            convert_out[64 + slice] = _ASSIGN_INDEXED((slice), port_b.convert_out[slice]());
        }
        port_b._assign();
        port_a.transmit_in = _ASSIGN(forwarding.tx_a_out());
        port_b.transmit_in = _ASSIGN(forwarding.tx_b_out());
        forwarding.rx_a_in = _ASSIGN(port_a.receive_out());
        forwarding.rx_b_in = _ASSIGN(port_b.receive_out());
        forwarding.valid_a_in = _ASSIGN(port_a.receive_valid_out());
        forwarding.valid_b_in = _ASSIGN(port_b.receive_valid_out());
        forwarding.fault_a_in = _ASSIGN(port_a.fault_out());
        forwarding.fault_b_in = _ASSIGN(port_b.fault_out());
        forwarding.rx_a_ready_in = _ASSIGN(port_a.rx_ready_out());
        forwarding.rx_b_ready_in = _ASSIGN(port_b.rx_ready_out());
        forwarding.tx_a_ready_in = _ASSIGN(port_a.tx_ready_out());
        forwarding.tx_b_ready_in = _ASSIGN(port_b.tx_ready_out());
        forwarding.enable_in = _ASSIGN(enable_in() && power_good_in() && port_a.link_out() && port_b.link_out());
        forwarding.pause_a_enabled_in = _ASSIGN(port_a.pause_enabled_out());
        forwarding.pause_b_enabled_in = _ASSIGN(port_b.pause_enabled_out());
        forwarding.address_a_in = _ASSIGN(address_in[0]());
        forwarding.address_b_in = _ASSIGN(address_in[1]());
        active_out = _ASSIGN(enable_in() && power_good_in() && port_a.link_out() && port_b.link_out() &&
                             port_a.pause_enabled_out() && port_b.pause_enabled_out() && !forwarding.fault_out());
        fault_out = _ASSIGN(port_a.fault_out() || port_b.fault_out() ||
                            forwarding.fault_out() || processing_clock.fault_out());
        adc_clock._assign(); processing_clock._assign(); forwarding._assign();
    }
    void _work_ref_clk(bool reset) {
        adc_clock._work_ref_clk(reset); processing_clock._work_ref_clk(reset);
#ifndef SYNTHESIS
        if (reset) processing_clock._reset_pos_ref_clk();
#endif
        port_a._work_ref_clk(reset); port_b._work_ref_clk(reset);
    }
    void _strobe_ref_clk() {
        adc_clock._strobe_ref_clk(); processing_clock._strobe_ref_clk();
        port_a._strobe_ref_clk(); port_b._strobe_ref_clk();
    }
    void _work_adc_clk(bool reset) {
        adc_clock._work_adc_clk(reset);
        port_a._work_adc_clk(reset); port_b._work_adc_clk(reset);
    }
    void _strobe_adc_clk() {
        adc_clock._strobe_adc_clk();
        port_a._strobe_adc_clk(); port_b._strobe_adc_clk();
    }
    void _work_raw_clk(bool reset) {
        processing_clock._work_pcs_clk(reset);
#ifndef SYNTHESIS
        if (reset) processing_clock._reset_pos_pcs_clk();
#endif
    }
    void _strobe_raw_clk() { processing_clock._strobe_pcs_clk(); }
    void _work_pcs_clk(bool reset) {
        port_a._work_pcs_clk(reset); port_b._work_pcs_clk(reset);
        forwarding._work_rx_clk(reset); forwarding._work_b_rx_clk(reset);
    }
    void _strobe_pcs_clk() {
        port_a._strobe_pcs_clk(); port_b._strobe_pcs_clk();
        forwarding._strobe_rx_clk(); forwarding._strobe_b_rx_clk();
    }
    void _work_tx_a_clk(bool reset) { port_a._work_tx_clk(reset); forwarding._work_a_tx_clk(reset); }
    void _work_tx_b_clk(bool reset) { port_b._work_tx_clk(reset); forwarding._work_tx_clk(reset); }
    void _strobe_tx_a_clk() { port_a._strobe_tx_clk(); forwarding._strobe_a_tx_clk(); }
    void _strobe_tx_b_clk() { port_b._strobe_tx_clk(); forwarding._strobe_tx_clk(); }
#ifndef SYNTHESIS
    void sim_assert_reset() {
        processing_clock._reset_pos_ref_clk(); processing_clock._reset_pos_pcs_clk();
        port_a.sim_assert_reset(); port_b.sim_assert_reset(); forwarding.sim_assert_reset();
    }
#endif
};
