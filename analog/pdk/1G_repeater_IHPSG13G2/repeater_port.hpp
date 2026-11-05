#pragma once
#include "../../1G/qualified_adc_receive_phy.hpp"
#include "../../1G/autonegotiation.hpp"
#include "../../1G/pma_control.hpp"
#include "../../1G/pcs_transmit.hpp"
#include "../../1G/pma_transmit_lane.hpp"
#include "../../1G/control_crossing.hpp"

// One copper port's digital circuitry. Clock inputs belong to the physical
// clock generators: tx_clk must follow recovered timing when this port is a
// slave. ADC comparisons and aperture-aligned local TX history cross the
// electrical boundary; this class does not substitute ideal PHY samples.
class RepeaterPort : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) power_good_in;
    _PORT(bool) adc_ready_in;
    _PORT(bool) pcs_ready_in;
    _PORT(bool) tx_ready_in;
    _PORT(bool) signal_ready_in;
    _PORT(bool) pulse_in;
    _PORT(bool) restart_in;
    _PORT(u<11>) configuration_in;
    _PORT(u<11>) negotiation_seed_in;
    _PORT(u<33>) scrambler_seed_in;
    _PORT(u<16>) comparisons_in[4];
    _PORT(u<8>) offset_in[4];
    _PORT(u<16>) gain_in[4];
    _PORT(u<12>) local_history_in;
    _PORT(u<10>) transmit_in;

    _PORT(bool) clear_out[4], evaluate_enable_out[4];
    _PORT(u<16>) aperture_out;
    _PORT(u<8>) trial_out[64];
    _PORT(bool) acquire_out[64];
    _PORT(bool) convert_out[64];
    _PORT(u<4>) main_p_out[4], main_n_out[4];
    _PORT(u<4>) history_p_out[4], history_n_out[4];
    _PORT(u<10>) receive_out;
    _PORT(bool) receive_valid_out;
    _PORT(bool) rx_ready_out;
    _PORT(bool) tx_ready_out;
    _PORT(bool) link_out;
    _PORT(bool) pause_enabled_out;
    _PORT(bool) master_out;
    _PORT(bool) pulse_out;
    _PORT(bool) pulse_enable_out;
    _PORT(bool) fault_out;
    _PORT(u<12>) symbols_out;
    _PORT(u<25>) recovered_period_out;
    _PORT(u<16>) recovered_tag_out;
    _PORT(bool) recovered_valid_out;

private:
    QualifiedADCReceivePHY receiver;
    Autonegotiation negotiation;
    ReferenceMillisecond millisecond;
    PMAControl pma;
    PCSTransmitter transmitter;
    PMATransmitLane drivers[4];
    reg<u<16>> sample_time{};
    // Each status is a stable level, not a one-cycle receive-valid event.
    // (* ASYNC_REG = "TRUE" *)
    reg<u<4>> status_ref1{}, status_ref2{};
    ControlCrossing<4> tx_control;
    ControlCrossing<2> rx_control;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> tx_active_ref1{}, tx_active_ref2{};

    _LAZY_COMB(available, bool)
        return available = enable_in() && power_good_in() && !restart_in();
    }
    _LAZY_COMB(receive_enabled, bool)
        return receive_enabled = available_func() && rx_control.valid_out() && (rx_control.data_out() & 1);
    }
    _LAZY_COMB(transmit_enabled, bool)
        return transmit_enabled = available_func() && tx_ready_in() && tx_control.valid_out() && (tx_control.data_out() & 3) == 2;
    }
    _LAZY_COMB(driver_fault, bool)
        return driver_fault = drivers[0].fault_out() || drivers[1].fault_out() ||
                              drivers[2].fault_out() || drivers[3].fault_out();
    }

public:
    void _assign() {
        tx_control.data_in = _ASSIGN(pma.mode_out() | (u<4>(pma.master_out()) << 2) |
                                     (u<4>((status_ref2 & 3) == 3 && !(status_ref2 & 8)) << 3));
        rx_control.data_in = _ASSIGN(u<2>(negotiation.pma_enable_out()) |
                                     (u<2>(negotiation.master_out()) << 1));
        tx_control._assign(); rx_control._assign();
        negotiation.enable_in = _ASSIGN(available_func());
        negotiation.restart_in = _ASSIGN(restart_in());
        negotiation.pulse_in = _ASSIGN(pulse_in());
        negotiation.pma_link_in = _ASSIGN(pma.link_out());
        negotiation.local_up1_in = _ASSIGN(configuration_in());
        negotiation.entropy_seed_in = _ASSIGN(negotiation_seed_in());
        pma.enable_in = _ASSIGN(available_func() && negotiation.pma_enable_out() && tx_ready_in());
        pma.restart_in = _ASSIGN(restart_in());
        pma.master_in = _ASSIGN(negotiation.master_out());
        pma.tick_ms_in = _ASSIGN(millisecond.tick_out());
        pma.scrambler_ok_in = _ASSIGN((status_ref2 & 1) != 0);
        pma.local_receiver_ok_in = _ASSIGN((status_ref2 & 3) == 3 && !(status_ref2 & 8));
        pma.remote_receiver_ok_in = _ASSIGN((status_ref2 & 4) != 0);
        pma.transmit_active_in = _ASSIGN(tx_active_ref2 != 0);

        receiver.enable_in = _ASSIGN(receive_enabled_func());
        receiver.restart_in = _ASSIGN(restart_in());
        receiver.power_good_in = _ASSIGN(power_good_in());
        receiver.clock_ready_in = _ASSIGN(adc_ready_in());
        receiver.processing_ready_in = _ASSIGN(pcs_ready_in());
        receiver.signal_ready_in = _ASSIGN(signal_ready_in());
        receiver.transport_fault_in = _ASSIGN(false);
        receiver.remote_master_in = _ASSIGN(!(rx_control.data_out() & 2));
        receiver.time_in = _ASSIGN_REG(sample_time);
        receiver.origin_in = _ASSIGN(0);
        receiver.initial_phase_in = _ASSIGN(0);
        receiver.track_in = _ASSIGN(true);
        receiver.sample_enable_in = _ASSIGN(!receiver.reset_adc_out());
        receiver.local_in = _ASSIGN(local_history_in());
        aperture_out = _ASSIGN(receiver.aperture_out());
        for (unsigned lane = 0; lane < 4; ++lane) {
            clear_out[lane] = _ASSIGN_INDEXED((lane), receiver.clear_out[lane]());
            evaluate_enable_out[lane] = _ASSIGN_INDEXED((lane), receiver.evaluate_enable_out[lane]());
            receiver.comparisons_in[lane] = _ASSIGN_INDEXED((lane), comparisons_in[lane]());
            receiver.offset_in[lane] = _ASSIGN_INDEXED((lane), offset_in[lane]());
            receiver.gain_in[lane] = _ASSIGN_INDEXED((lane), gain_in[lane]());
        }
        for (unsigned slice = 0; slice < 64; ++slice) {
            trial_out[slice] = _ASSIGN_INDEXED((slice), receiver.trial_out[slice]());
            acquire_out[slice] = _ASSIGN_INDEXED((slice), receiver.acquire_out[slice]());
            convert_out[slice] = _ASSIGN_INDEXED((slice), receiver.convert_out[slice]());
        }
        transmitter.data_in = _ASSIGN(transmit_in() & 255);
        transmitter.enable_in = _ASSIGN(transmit_enabled_func() && (transmit_in() & 256));
        transmitter.error_in = _ASSIGN((transmit_in() & 512) != 0);
        transmitter.mode_in = _ASSIGN(tx_control.data_out() & 3);
        transmitter.master_in = _ASSIGN((tx_control.data_out() & 4) != 0);
        transmitter.link_in = _ASSIGN((tx_control.data_out() & 3) == 2);
        transmitter.local_receiver_ok_in = _ASSIGN((tx_control.data_out() & 8) != 0);
        transmitter.seed_load_in = _ASSIGN((tx_control.data_out() & 3) == 0);
        transmitter.seed_in = _ASSIGN(scrambler_seed_in());
        for (unsigned lane = 0; lane < 4; ++lane) {
            drivers[lane].enable_in = _ASSIGN(available_func() && tx_ready_in() && tx_control.valid_out() && (tx_control.data_out() & 3) != 0);
            drivers[lane].symbol_in = _ASSIGN_INDEXED((lane), (transmitter.symbols_out() >> (3 * lane)) & 7);
            drivers[lane].test_mode_in = _ASSIGN(0);
            drivers[lane].test_seed_in = _ASSIGN(1);
            drivers[lane].restart_in = _ASSIGN(restart_in());
            main_p_out[lane] = _ASSIGN_INDEXED((lane), drivers[lane].main_p_out());
            main_n_out[lane] = _ASSIGN_INDEXED((lane), drivers[lane].main_n_out());
            history_p_out[lane] = _ASSIGN_INDEXED((lane), drivers[lane].history_p_out());
            history_n_out[lane] = _ASSIGN_INDEXED((lane), drivers[lane].history_n_out());
            drivers[lane]._assign();
        }
        receive_out = _ASSIGN(receiver.receive_out());
        receive_valid_out = _ASSIGN(receiver.receive_valid_out());
        rx_ready_out = _ASSIGN(receive_enabled_func() && adc_ready_in() && pcs_ready_in() &&
                               !receiver.reset_pcs_out() && !receiver.fault_out());
        tx_ready_out = _ASSIGN(available_func() && tx_ready_in() && (tx_control.data_out() & 3) == 2);
        link_out = _ASSIGN(available_func() && negotiation.link_out() && pma.link_out());
        pause_enabled_out = _ASSIGN(negotiation.pause_out());
        master_out = _ASSIGN(negotiation.master_out());
        pulse_out = _ASSIGN(negotiation.pulse_out());
        pulse_enable_out = _ASSIGN(available_func() && !negotiation.pma_enable_out());
        fault_out = _ASSIGN(negotiation.fault_out() || pma.timeout_out() || receiver.fault_out() || driver_fault_func());
        symbols_out = _ASSIGN(transmitter.symbols_out());
        recovered_period_out = _ASSIGN(receiver.period_out());
        recovered_tag_out = _ASSIGN(receiver.recovered_tag_out());
        recovered_valid_out = _ASSIGN(receiver.recovered_valid_out());
        negotiation._assign();
        millisecond._assign();
        pma._assign();
        receiver._assign();
        transmitter._assign();
    }

    void _work_ref_clk(bool reset) {
        status_ref1._next = unsigned(receiver.locked_out()) | (unsigned(receiver.quality_out()) << 1) |
                            (unsigned(receiver.remote_receiver_ok_out()) << 2) | (unsigned(receiver.fault_out()) << 3);
        status_ref2._next = status_ref1;
        tx_active_ref1._next = (transmit_in() & 256) != 0;
        tx_active_ref2._next = tx_active_ref1;
        negotiation._work(reset);
        millisecond._work(reset || !available_func());
        pma._work(reset);
        receiver._work_ref_clk(reset);
#ifndef SYNTHESIS
        if (reset || !receive_enabled_func() || !adc_ready_in() || !pcs_ready_in())
            receiver.sim_assert_ref_reset();
#endif
        tx_control._work_source_clk(reset || !available_func() || !tx_ready_in());
        rx_control._work_source_clk(reset || !available_func() || !adc_ready_in() || !pcs_ready_in());
#ifndef SYNTHESIS
        if (reset || !available_func() || !tx_ready_in()) tx_control._reset_pos_source_clk();
        if (reset || !available_func() || !adc_ready_in() || !pcs_ready_in()) rx_control._reset_pos_source_clk();
#endif
        if (reset || !available_func()) {
            status_ref1._next = 0; status_ref2._next = 0;
            tx_active_ref1._next = 0; tx_active_ref2._next = 0;
        }
    }
    void _strobe_ref_clk() {
        status_ref1.strobe(); status_ref2.strobe();
        tx_active_ref1.strobe(); tx_active_ref2.strobe();
        negotiation._strobe(); millisecond._strobe(); pma._strobe();
        receiver._strobe_ref_clk();
        tx_control._strobe_source_clk(); rx_control._strobe_source_clk();
    }
    void _work_adc_clk(bool reset) {
        sample_time._next = sample_time + 1;
        receiver._work_adc_clk(reset);
#ifndef SYNTHESIS
        if (reset || !receive_enabled_func() || !adc_ready_in() || !pcs_ready_in())
            receiver.sim_assert_adc_reset();
#endif
        if (reset || !available_func()) sample_time._next = 0;
    }
    void _strobe_adc_clk() { sample_time.strobe(); receiver._strobe_adc_clk(); }
    void _work_pcs_clk(bool reset) {
        rx_control._work_destination_clk(reset || !available_func() || !adc_ready_in() || !pcs_ready_in());
        receiver._work_pcs_clk(reset);
#ifndef SYNTHESIS
        if (reset || !receive_enabled_func() || !adc_ready_in() || !pcs_ready_in())
            receiver.sim_assert_pcs_reset();
#endif
#ifndef SYNTHESIS
        if (reset || !available_func() || !adc_ready_in() || !pcs_ready_in()) rx_control._reset_pos_destination_clk();
#endif
    }
    void _strobe_pcs_clk() {
        rx_control._strobe_destination_clk(); receiver._strobe_pcs_clk();
    }
    void _work_tx_clk(bool reset) {
        tx_control._work_destination_clk(reset || !available_func() || !tx_ready_in());
        transmitter._work(reset || !available_func() || !tx_ready_in());
        for (unsigned lane = 0; lane < 4; ++lane) drivers[lane]._work(reset);
#ifndef SYNTHESIS
        if (reset || !available_func() || !tx_ready_in()) tx_control._reset_pos_destination_clk();
#endif
    }
    void _strobe_tx_clk() {
        tx_control._strobe_destination_clk(); transmitter._strobe();
        for (unsigned lane = 0; lane < 4; ++lane) drivers[lane]._strobe();
    }
#ifndef SYNTHESIS
    void sim_assert_reset() {
        tx_control._reset_pos_source_clk(); tx_control._reset_pos_destination_clk();
        rx_control._reset_pos_source_clk(); rx_control._reset_pos_destination_clk();
        receiver.sim_assert_ref_reset(); receiver.sim_assert_adc_reset(); receiver.sim_assert_pcs_reset();
    }
#endif
};
