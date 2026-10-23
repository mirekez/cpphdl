#pragma once
#include "flp_transmit.hpp"
#include "flp_receive.hpp"
#include "negotiation.hpp"

class ReferenceMillisecond : public Module {
public:
    _PORT(bool) tick_out = _ASSIGN_REG(tick);
private:
    reg<u<17>> count{};
    reg<u1> tick{};
public:
    void _assign() {}
    void _work(bool reset) {
        tick._next = count == 124999;
        count._next = count == 124999 ? 0 : unsigned(count) + 1;
        if (reset) {
            count._next = 0;
            tick._next = 0;
        }
    }
    void _strobe() {
        count.strobe();
        tick.strobe();
    }
};

// One-reference digital negotiation path. Electrical pulse drive/detection,
// entropy generation and the trained PHY connect to these explicit ports.
class Autonegotiation : public Module {
public:
    _PORT(bool) pulse_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) pma_link_in;
    _PORT(u<11>) local_up1_in;
    _PORT(u<11>) entropy_seed_in;
    _PORT(bool) pulse_out = _ASSIGN(tx.pulse_out());
    _PORT(bool) pma_enable_out = _ASSIGN(control.pma_enable_out());
    _PORT(bool) link_out = _ASSIGN(control.link_out());
    _PORT(bool) master_out = _ASSIGN(control.master_out());
    _PORT(bool) fault_out = _ASSIGN(control.fault_out());
    _PORT(u<3>) reason_out = _ASSIGN(control.reason_out());
private:
    ReferenceMillisecond timebase;
    FLPTransmitter tx;
    FLPReceiver rx;
    Autonegotiator control;
public:
    void _assign() {
        tx.page_in = _ASSIGN(control.transmit_page_out());
        tx.enable_in = _ASSIGN(control.transmit_enable_out());
        tx.restart_in = _ASSIGN(restart_in());
        rx.pulse_in = _ASSIGN(pulse_in());
        rx.enable_in = _ASSIGN(control.transmit_enable_out());
        control.receive_page_in = _ASSIGN(rx.page_out());
        control.receive_valid_in = _ASSIGN(rx.valid_out());
        control.receive_fault_in = _ASSIGN(rx.fault_out());
        control.transmit_burst_in = _ASSIGN(tx.burst_done_out());
        control.tick_ms_in = _ASSIGN(timebase.tick_out());
        control.enable_in = _ASSIGN(enable_in());
        control.restart_in = _ASSIGN(restart_in());
        control.pma_link_in = _ASSIGN(pma_link_in());
        control.local_up1_in = _ASSIGN(local_up1_in());
        control.entropy_seed_in = _ASSIGN(entropy_seed_in());
        timebase._assign();
        tx._assign();
        rx._assign();
        control._assign();
    }
    void _work(bool reset) {
        timebase._work(reset || restart_in() || !enable_in());
        tx._work(reset);
        rx._work(reset);
        control._work(reset);
    }
    void _strobe() {
        timebase._strobe();
        tx._strobe();
        rx._strobe();
        control._strobe();
    }
};
