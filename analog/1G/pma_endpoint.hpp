#pragma once
#include "pma_control.hpp"
#include "pcs_transmit.hpp"
#include "pcs_receiver.hpp"

// Symbol-domain PHY composition. ADC sampling, pair alignment, clock recovery
// and receiver-quality qualification connect through explicit physical ports.
// The receiver decodes the peer's readiness from actual received idle symbols.
class PMAEndpoint : public Module {
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
    _PORT(u<12>) symbols_out = _ASSIGN(tx.symbols_out());
    _PORT(u<10>) receive_out = _ASSIGN(rx.receive_out());
    _PORT(bool) link_out = _ASSIGN(control.link_out());
    _PORT(bool) timeout_out = _ASSIGN(control.timeout_out());
    _PORT(bool) locked_out = _ASSIGN(rx.locked_out());
    _PORT(u<2>) mode_out = _ASSIGN(control.mode_out());
private:
    PMAControl control;
    PCSTransmitter tx;
    PCSReceiver rx;
public:
    void _assign() {
        control.enable_in = _ASSIGN(enable_in());
        control.restart_in = _ASSIGN(restart_in());
        control.master_in = _ASSIGN(master_in());
        control.tick_ms_in = _ASSIGN(tick_ms_in());
        control.scrambler_ok_in = _ASSIGN(rx.locked_out());
        control.local_receiver_ok_in = _ASSIGN(rx.locked_out() && samples_valid_in() && receiver_quality_in());
        control.remote_receiver_ok_in = _ASSIGN(rx.remote_receiver_ok_out());
        control.transmit_active_in = _ASSIGN(transmit_enable_in());
        tx.data_in = _ASSIGN(transmit_data_in());
        tx.enable_in = _ASSIGN(transmit_enable_in());
        tx.error_in = _ASSIGN(transmit_error_in());
        tx.master_in = _ASSIGN(control.master_out());
        tx.mode_in = _ASSIGN(control.mode_out());
        // Retain SEND_N through an in-progress frame until control changes mode.
        tx.link_in = _ASSIGN(control.mode_out() == 2);
        tx.local_receiver_ok_in = _ASSIGN(rx.locked_out() && samples_valid_in() && receiver_quality_in());
        tx.seed_load_in = _ASSIGN(control.state_out() == 0 && enable_in());
        tx.seed_in = _ASSIGN(seed_in());
        rx.symbol_enable_in = _ASSIGN(true);
        rx.samples_in = _ASSIGN(samples_in());
        rx.remote_master_in = _ASSIGN(!control.master_out());
        rx.pma_ok_in = _ASSIGN(enable_in() && !restart_in() && samples_valid_in());
        control._assign();
        tx._assign();
        rx._assign();
    }
    void _work(bool reset) {
        control._work(reset);
        tx._work(reset || restart_in() || !enable_in());
        rx._work(reset || restart_in() || !enable_in());
    }
    void _strobe() {
        control._strobe();
        tx._strobe();
        rx._strobe();
    }
};
